#include "pch.h"
#include "v8-util.h"

using v8::V8;

namespace JSVM
{
	enum ISOLATE_DATA_SLOT
	{
		ISOLATE_DATA_SLOT_LOG_CHANNEL,
	};
	struct jsvm_persistent_tmpl_impl :public jsvm_persistent_tmpl
	{
		explicit jsvm_persistent_tmpl_impl(v8::Isolate* iso, v8::Local<v8::Template> value)
			: isolate(iso), tmpl_persistent(isolate, value)
		{
		}
		v8::Isolate* const isolate;
		v8::Global<v8::Template> tmpl_persistent;
	};

	struct jsvm_persistent_value_impl :public jsvm_persistent_value
	{
		explicit jsvm_persistent_value_impl(v8::Isolate* iso, v8::Local<v8::Value> value)
			: isolate(iso), value_persistent(isolate, value)
		{
		}
		v8::Isolate* const isolate;
		v8::Global<v8::Value> value_persistent;
	};

	struct jsvm_vm_impl;

	struct jsvm_handle_scope_impl :public jsvm_handle_scope
	{
		explicit jsvm_handle_scope_impl(jsvm_vm_impl* jsvm);
		jsvm_vm_impl* vm;
		v8::Isolate* isolate;
		v8::Locker Locker;
		v8::HandleScope scope;
		v8::Local<v8::Context> content;
		v8::Isolate::Scope isolate_scope;
		v8::Context::Scope context_scope;
		v8::TryCatch trycatch;
	};

	struct jsvm_vm_impl :public jsvm_vm
	{
	protected:
		CEasyArray<jsvm_handle_scope_impl*> scope_list;		
	public:
		UINT ConsoleLogChannel;
		uv_async_t keepalive_async_handle;
		std::unique_ptr<node::CommonEnvironmentSetup> setup;
		jsvm_vm_impl()
			:scope_list(32, 32)
		{
			ConsoleLogChannel = LOG_JSVM_CHANNEL;
			keepalive_async_handle.data = NULL;
		}
		~jsvm_vm_impl()
		{
			for (jsvm_handle_scope_impl* scope : scope_list)
			{
				delete scope;
			}
			scope_list.Clear();

			if (keepalive_async_handle.data)
			{
				uv_close(reinterpret_cast<uv_handle_t*>(&keepalive_async_handle), nullptr);
				keepalive_async_handle.data = NULL;
			}

			if (setup.get() != NULL)
				node::Stop(setup->env());
			setup.reset();
		}
		jsvm_handle_scope_impl* CreateScope()
		{
			jsvm_handle_scope_impl* scope = new jsvm_handle_scope_impl(this);
			scope_list.Add(scope);
			return scope;
		}
		bool ReleaseScope(jsvm_handle_scope_impl* scope)
		{
			scope_list.Remove(scope);
			delete scope;
			return true;
		}
	};

	jsvm_handle_scope_impl::jsvm_handle_scope_impl(jsvm_vm_impl* jsvm)
		: vm(jsvm), isolate(vm->setup->isolate()), Locker(isolate), scope(isolate), content(vm->setup->context()), isolate_scope(isolate), context_scope(content), trycatch(isolate)
	{
	}


	struct jsvm_platform
	{
	protected:
		CEasyCriticalSection CriticalSection;
		CEasyArray<jsvm_vm_impl*> vm_list;
	public:
		std::unique_ptr<node::MultiIsolatePlatform> platform;
		std::vector<std::string> args;

		friend bool jsvm_platform_init(LPCSTR params);
		friend void jsvm_platform_dispose();

		jsvm_platform()
			:vm_list(16, 16)
		{

		}

		jsvm_vm_impl* AllocVM()
		{
			CAutoLock Lock(CriticalSection);
			jsvm_vm_impl* pVM = new jsvm_vm_impl;
			vm_list.Add(pVM);
			return pVM;
		}
		bool FreeVM(jsvm_vm_impl* pVM);
	};

	jsvm_platform g_jsvm_platform;

	bool jsvm_platform::FreeVM(jsvm_vm_impl* pVM)
	{
		CAutoLock Lock(CriticalSection);
		g_jsvm_platform.vm_list.Remove(pVM);
		delete pVM;
		return true;
	}


	void CallbackAdapter(const v8::FunctionCallbackInfo<v8::Value>& args)
	{
		auto callback = reinterpret_cast<jsvm_callback>((v8::Local<v8::External>::Cast(args.Data()))->Value());
		callback((jsvm_callback_info*)(&args));
	}





	void parser_args(LPCSTR params, std::vector<std::string>& args)
	{
		CStringSplitter Splitter(params, "--");
		for (UINT i = 0; i < Splitter.GetCount(); i++)
		{
			LPCSTR szParam = StringTrim((char*)Splitter[i], ' ');
			if (szParam[0])
			{
				if (i == 0)
				{
					args.push_back(szParam);
				}
				else
				{
					CEasyString Temp;
					Temp.Format("--%s", szParam);
					args.push_back((LPCSTR)Temp);
				}
			}
		}
	}

	void PrintStackTrace(v8::Isolate* isolate, v8::Local<v8::StackTrace> stack_trace, LPCSTR Tag, int Level)
	{
		v8::HandleScope handle_scope(isolate);

		int frame_count = stack_trace->GetFrameCount();
		for (int i = 0; i < frame_count; i++) {
			v8::Local<v8::StackFrame> frame = stack_trace->GetFrame(isolate, i);
			v8::String::Utf8Value script_name(isolate, frame->GetScriptName());
			v8::String::Utf8Value function_name(isolate, frame->GetFunctionName());
			int line_number = frame->GetLineNumber();
			int column = frame->GetColumn();

			UINT LogChannel = static_cast<UINT>(reinterpret_cast<uintptr_t>(isolate->GetData(ISOLATE_DATA_SLOT_LOG_CHANNEL)));
			CLogManager::GetInstance()->PrintLog(LogChannel, Level, Tag, "    at %s:%s(%d:%d)",
				*script_name,
				(function_name.length() > 0) ? *function_name : "<anonymous> ",
				line_number, column);
		}
	}
	v8::MaybeLocal<v8::Module> ResolveModuleCallback(v8::Local<v8::Context> context, v8::Local<v8::String> specifier, v8::Local<v8::FixedArray> import_attributes, v8::Local<v8::Module> referrer)
	{
		v8::Isolate* isolate = context->GetIsolate();
		v8::String::Utf8Value specifier_utf8(isolate, specifier);

		CStringFile ScriptFile;
		ScriptFile.SetLocalCodePage(CP_UTF8);
		if (ScriptFile.LoadFile(*specifier_utf8, false))
		{
			v8::ScriptOrigin origin(specifier, 0, 0, false, -1, v8::Local<v8::Value>(), false, false, true);
			v8::ScriptCompiler::Source source(v8::String::NewFromUtf8(isolate, ScriptFile.GetData()).ToLocalChecked(), origin);
			auto module = v8::ScriptCompiler::CompileModule(isolate, &source);

			if (!module.IsEmpty())
			{
				auto mod = module.ToLocalChecked();
				if (mod->InstantiateModule(context, ResolveModuleCallback).IsNothing()) {
					isolate->ThrowException(v8::String::NewFromUtf8Literal(isolate, "Failed to instantiate module"));
					return v8::MaybeLocal<v8::Module>();
				}
				mod->Evaluate(context);
			}
			return module;
		}
		else
		{
			CEasyString Msg;
			Msg.Format("open %s failed", *specifier_utf8);
			isolate->ThrowError(v8::String::NewFromUtf8(isolate, Msg).ToLocalChecked());
			return v8::MaybeLocal<v8::Module>();
		}
	}
	v8::MaybeLocal<v8::Promise> ImportModuleDynamically(v8::Local<v8::Context> context, v8::Local<v8::Data> host_defined_options, v8::Local<v8::Value> resource_name, v8::Local<v8::String> specifier, v8::Local<v8::FixedArray> import_assertions)
	{
		v8::Isolate* isolate = context->GetIsolate();
		v8::EscapableHandleScope handle_scope(isolate);

		v8::String::Utf8Value ResName(isolate, resource_name);
		v8::String::Utf8Value Spec(isolate, specifier);

		CStringFile ScriptFile;
		ScriptFile.SetLocalCodePage(CP_UTF8);
		if (ScriptFile.LoadFile(*Spec, false))
		{
			v8::ScriptOrigin origin(specifier, 0, 0, false, -1, v8::Local<v8::Value>(), false, false, true);
			v8::ScriptCompiler::Source source(v8::String::NewFromUtf8(isolate, ScriptFile.GetData()).ToLocalChecked(), origin);
			auto module = v8::ScriptCompiler::CompileModule(isolate, &source);

			if (!module.IsEmpty())
			{
				auto mod = module.ToLocalChecked();
				if (!mod->InstantiateModule(context, ResolveModuleCallback).IsNothing()) {
					auto result = mod->Evaluate(context);
					if (!result.IsEmpty() && result.ToLocalChecked()->IsPromise())
						return result.ToLocalChecked().As<v8::Promise>();
				}
			}
		}
		auto resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
		resolver->Resolve(context, v8::Undefined(isolate));
		return handle_scope.Escape(resolver->GetPromise());
	}



	void V8ValueToStr(v8::Isolate* isolate, v8::Local<v8::Value> Value, char* StrBuff, size_t BuffLen)
	{
		if(!Value.IsEmpty())
		{
			v8::Local<v8::String> Str;
			if (Value->IsSymbol())
			{
				Str = Value.As<v8::Symbol>()->Description(isolate).As<v8::String>();
			}
			else
			{
				Value->ToString(isolate->GetCurrentContext()).ToLocal(&Str);
			}
			if (!Str.IsEmpty())
			{
				if (CEasyStringA::SYSTEM_STRING_CODE_PAGE == CEasyStringA::STRING_CODE_PAGE_UTF8)
				{
					Str->WriteUtf8(isolate, StrBuff, (int)BuffLen);
					StrBuff[BuffLen - 1] = 0;
				}
				else
				{
					WCHAR TransBuffer[4096];
					int UCS2Len = Str->Write(isolate, (uint16_t*)TransBuffer, 0, (int)BuffLen);
					BuffLen = UnicodeToAnsi(TransBuffer, UCS2Len, StrBuff, BuffLen);
					StrBuff[BuffLen - 1] = 0;
				}
			}
			else
			{
				strcpy_s(StrBuff, BuffLen, "NULL");
			}
		}
		else
		{
			strcpy_s(StrBuff, BuffLen, "NULL");
		}
	}

	bool ProcessException(jsvm_handle_scope_impl* scope_impl)
	{
		if (scope_impl->trycatch.HasCaught())
		{
			v8::Local<v8::Value> Msg;
			scope_impl->trycatch.StackTrace(scope_impl->content).ToLocal(&Msg);
			char Buff[4096];
			V8ValueToStr(scope_impl->isolate, Msg, Buff, 4096);
			UINT LogChannel = static_cast<UINT>(reinterpret_cast<uintptr_t>(scope_impl->isolate->GetData(ISOLATE_DATA_SLOT_LOG_CHANNEL)));
			CLogManager::GetInstance()->PrintLogDirect(LogChannel, ILogPrinter::LOG_LEVEL_NORMAL, "exception", Buff);
			scope_impl->trycatch.Reset();
			return true;
		}
		return false;
	}

	void console_log(const v8::FunctionCallbackInfo<v8::Value>& args) {
		v8::Isolate* isolate = args.GetIsolate();
		char Buff[4096];
		for (int i = 0; i < args.Length(); i++)
		{
			V8ValueToStr(isolate, args[i], Buff, 4096);
			UINT LogChannel = static_cast<UINT>(reinterpret_cast<uintptr_t>(isolate->GetData(ISOLATE_DATA_SLOT_LOG_CHANNEL)));
			CLogManager::GetInstance()->PrintLogDirect(LogChannel, ILogPrinter::LOG_LEVEL_DEBUG, "console_log", Buff);
		}
	}
	void console_warn(const v8::FunctionCallbackInfo<v8::Value>& args) {
		v8::Isolate* isolate = args.GetIsolate();
		char Buff[4096];
		for (int i = 0; i < args.Length(); i++)
		{
			V8ValueToStr(isolate, args[i], Buff, 4096);
			UINT LogChannel = static_cast<UINT>(reinterpret_cast<uintptr_t>(isolate->GetData(ISOLATE_DATA_SLOT_LOG_CHANNEL)));
			CLogManager::GetInstance()->PrintLogDirect(LogChannel, ILogPrinter::LOG_LEVEL_NORMAL, "console_warn", Buff);
		}
	}
	void console_error(const v8::FunctionCallbackInfo<v8::Value>& args) {
		v8::Isolate* isolate = args.GetIsolate();
		char Buff[4096];
		for (int i = 0; i < args.Length(); i++)
		{
			V8ValueToStr(isolate, args[i], Buff, 4096);
			UINT LogChannel = static_cast<UINT>(reinterpret_cast<uintptr_t>(isolate->GetData(ISOLATE_DATA_SLOT_LOG_CHANNEL)));
			CLogManager::GetInstance()->PrintLogDirect(LogChannel, ILogPrinter::LOG_LEVEL_NORMAL, "console_error", Buff);
		}
	}
	void console_trace(const v8::FunctionCallbackInfo<v8::Value>& args) {
		v8::Isolate* isolate = args.GetIsolate();
		char Buff[4096];
		for (int i = 0; i < args.Length(); i++)
		{
			V8ValueToStr(isolate, args[i], Buff, 4096);
			UINT LogChannel = static_cast<UINT>(reinterpret_cast<uintptr_t>(isolate->GetData(ISOLATE_DATA_SLOT_LOG_CHANNEL)));
			CLogManager::GetInstance()->PrintLogDirect(LogChannel, ILogPrinter::LOG_LEVEL_DEBUG, "console_trace", Buff);
			//v8::Local<v8::StackTrace> stack_trace = v8::StackTrace::CurrentStackTrace(isolate, 20);
			//PrintStackTrace(isolate, stack_trace, "trace", ILogPrinter::LOG_LEVEL_DEBUG);
		}
	}
	void console_assert(const v8::FunctionCallbackInfo<v8::Value>& args) {
		v8::Isolate* isolate = args.GetIsolate();
		char Buff[4096];
		if (args.Length() >= 2)
		{
			if (!args[0]->BooleanValue(isolate))
			{
				for (int i = 1; i < args.Length(); i++)
				{
					V8ValueToStr(isolate, args[i], Buff, 4096);
					UINT LogChannel = static_cast<UINT>(reinterpret_cast<uintptr_t>(isolate->GetData(ISOLATE_DATA_SLOT_LOG_CHANNEL)));
					CLogManager::GetInstance()->PrintLogDirect(LogChannel, ILogPrinter::LOG_LEVEL_NORMAL, "console_assert", Buff);
				}
			}
		}
	}

	class FileStreamForDump : public v8::OutputStream
	{
	protected:
		IFileAccessor* m_File;
	public:
		~FileStreamForDump()
		{
			Close();
		}
		bool Open(LPCTSTR szFilePath)
		{
			Close();
			m_File = CFileSystemManager::GetInstance()->CreateFileAccessor(FILE_CHANNEL_NORMAL1);
			if (m_File)
			{
				if (m_File->Open(szFilePath, IFileAccessor::modeWrite | IFileAccessor::shareShareRead | IFileAccessor::modeCreateAlways))
					return true;
			}
			return false;
		}
		void Close()
		{
			SAFE_RELEASE(m_File);
		}
		virtual int GetChunkSize() override {
			return 1024;
		}

		virtual void EndOfStream() override {
			Close();
		}

		virtual WriteResult WriteAsciiChunk(char* data, int size) override {
			m_File->Write(data, size);
			return kContinue;
		}

	};

	template<typename T, typename LocalT>
	inline T* v8_local_to_jsvm_ptr(v8::Local<LocalT> local)
	{
		return reinterpret_cast<T*>(*local);
	}

	template<typename T, typename LocalT>
	inline v8::Local<LocalT> jsvm_ptr_to_v8_local(T* value)
	{
		v8::Local<LocalT> local;
		memcpy(static_cast<void*>(&local), &value, sizeof(value));
		return local;
	}

#define v8_local_value_to_jsvm_value(local) v8_local_to_jsvm_ptr<jsvm_value, v8::Value>(local)
#define jsvm_value_to_v8_local_value(ptr) jsvm_ptr_to_v8_local<jsvm_value, v8::Value>(ptr)

#define v8_local_tmpl_to_jsvm_tmpl(local) v8_local_to_jsvm_ptr<jsvm_tmpl, v8::Template>(local)
#define jsvm_tmpl_to_v8_local_tmpl(ptr) jsvm_ptr_to_v8_local<jsvm_tmpl, v8::Template>(ptr)
#define jsvm_tmpl_to_v8_local_func_tmpl(ptr) jsvm_ptr_to_v8_local<jsvm_tmpl, v8::FunctionTemplate>(ptr)

#define v8_local_context_to_jsvm_context(local) v8_local_to_jsvm_ptr<jsvm_context, v8::Context>(local)
#define jsvm_context_to_v8_local_context(ptr) jsvm_ptr_to_v8_local<jsvm_context, v8::Context>(ptr)

	bool jsvm_platform_init(LPCSTR params)
	{
		CAutoLock Lock(g_jsvm_platform.CriticalSection);

		if (g_jsvm_platform.platform.get())
			jsvm_platform_dispose();

		parser_args(params, g_jsvm_platform.args);

		std::vector<std::string> args;
		args.push_back("");
		args.insert(args.end(), g_jsvm_platform.args.begin(), g_jsvm_platform.args.end());

		std::shared_ptr<node::InitializationResult> result =
			node::InitializeOncePerProcess(
				args,
				{
					node::ProcessInitializationFlags::kNoInitializeV8,
					node::ProcessInitializationFlags::kNoInitializeNodeV8Platform,
					node::ProcessInitializationFlags::kDisableNodeOptionsEnv,
				});

		for (const std::string& error : result->errors())
			LogJSVM("%s\n", error.c_str());
		if (result->early_return() != 0) {
			return false;
		}

		g_jsvm_platform.platform =
			node::MultiIsolatePlatform::Create(4);
		V8::InitializePlatform(g_jsvm_platform.platform.get());
		V8::Initialize();

		return true;
	}

	void jsvm_platform_dispose()
	{
		CAutoLock Lock(g_jsvm_platform.CriticalSection);

		if (g_jsvm_platform.platform.get())
		{
			for (int i = (int)g_jsvm_platform.vm_list.GetCount() - 1; i >= 0; i--)
			{
				jsvm_vm_dispose(g_jsvm_platform.vm_list[i]);
			}
			g_jsvm_platform.vm_list.Clear();
			g_jsvm_platform.args.clear();

			V8::Dispose();
			V8::DisposePlatform();
			g_jsvm_platform.platform.reset();
			node::TearDownOncePerProcess();
		}
	}

	jsvm_vm* jsvm_create_vm(LPCSTR script_root_dir, LPCSTR params, UINT Flags, child_vm_setting* child_setting, LPCSTR init_script, size_t script_len)
	{
		if (g_jsvm_platform.platform.get())
		{
			jsvm_vm_impl* pVM = g_jsvm_platform.AllocVM();

			std::vector<std::string> errors;

			std::vector<std::string> args = g_jsvm_platform.args;
			std::vector<std::string> exec_args;
			if (params)
				parser_args(params, exec_args);


			if (child_setting && child_setting->parent_vm && child_setting->child_thread_id)
			{
				jsvm_vm_impl* pParent = (jsvm_vm_impl*)child_setting->parent_vm;
				node::ThreadId thread_id;
				thread_id.id = child_setting->child_thread_id;
				auto parent_handle = node::GetInspectorParentHandle(pParent->setup->env(), thread_id, child_setting->child_url, child_setting->child_name);

				pVM->setup = node::CommonEnvironmentSetup::Create(g_jsvm_platform.platform.get(), &errors, args, exec_args, node::EnvironmentFlags::kNoWaitForInspectorFrontend, thread_id, std::move(parent_handle));
				pVM->setup->env()->WaitForInspectorFrontendByOptions();
			}
			else
			{
				pVM->setup = node::CommonEnvironmentSetup::Create(g_jsvm_platform.platform.get(), &errors, args, exec_args, (node::EnvironmentFlags::Flags)(node::EnvironmentFlags::kOwnsProcessState | node::EnvironmentFlags::kOwnsInspector));
			}

			if (pVM->setup)
			{
				node::Environment* env = pVM->setup->env();



				v8::Isolate* isolate = pVM->setup->isolate();
				v8::Locker locker(isolate);
				v8::HandleScope handle_scope(isolate);
				v8::Isolate::Scope isolate_scope(isolate);
				v8::Local<v8::Context> context = pVM->setup->context();
				v8::Context::Scope context_scope(context);

				pVM->setup->isolate()->SetData(ISOLATE_DATA_SLOT_LOG_CHANNEL, (void*)pVM->ConsoleLogChannel);

				if ((Flags & jsvm_not_redirect_console) == 0)
				{
					//重载console的几个输出函数
					v8::Local<v8::String> ObjName = v8::String::NewFromUtf8(isolate, "console").ToLocalChecked();
					auto value = context->Global()->Get(context, ObjName);
					if (!value.IsEmpty() && value.ToLocalChecked()->IsObject())
					{
						v8::Local<v8::Object> Console = value.ToLocalChecked().As<v8::Object>();

						v8::Local<v8::Function> Fun = v8::FunctionTemplate::New(isolate, console_log)->GetFunction(context).ToLocalChecked();
						v8::Local<v8::String> FunName = v8::String::NewFromUtf8(isolate, "log").ToLocalChecked();
						Console->Set(context, FunName, Fun);

						FunName = v8::String::NewFromUtf8(isolate, "info").ToLocalChecked();
						Console->Set(context, FunName, Fun);

						FunName = v8::String::NewFromUtf8(isolate, "debug").ToLocalChecked();
						Console->Set(context, FunName, Fun);

						Fun = v8::FunctionTemplate::New(isolate, console_warn)->GetFunction(context).ToLocalChecked();
						FunName = v8::String::NewFromUtf8(isolate, "warn").ToLocalChecked();
						Console->Set(context, FunName, Fun);

						Fun = v8::FunctionTemplate::New(isolate, console_error)->GetFunction(context).ToLocalChecked();
						FunName = v8::String::NewFromUtf8(isolate, "error").ToLocalChecked();
						Console->Set(context, FunName, Fun);

						Fun = v8::FunctionTemplate::New(isolate, console_trace)->GetFunction(context).ToLocalChecked();
						FunName = v8::String::NewFromUtf8(isolate, "trace").ToLocalChecked();
						Console->Set(context, FunName, Fun);

						Fun = v8::FunctionTemplate::New(isolate, console_assert)->GetFunction(context).ToLocalChecked();
						FunName = v8::String::NewFromUtf8(isolate, "assert").ToLocalChecked();
						Console->Set(context, FunName, Fun);
					}
				}
				if (Flags & jsvm_keep_event_loop_active)
				{
					uv_async_init(pVM->setup->event_loop(), &pVM->keepalive_async_handle, [](uv_async_t* handle)->void {});
					pVM->keepalive_async_handle.data = pVM;
				}

				v8::MaybeLocal<v8::Value> loadenv_ret;
				CEasyString InitScript;
				if (init_script)
				{
					InitScript.SetString(init_script, script_len);
				}
				else
				{
					CEasyString ScriptRoot(script_root_dir);

					ScriptRoot.Replace('\\', '/');

					InitScript.Format(R"(
									let _old_console_trace = console.trace;
									console.trace = (Msg) => {
										let err = new Error;
										err.name = Msg;
										_old_console_trace(err.stack);
									}
									process.on('uncaughtException', (err) => {
										console.trace(err);
										console.error(err.stack);
									});
									process.on('unhandledRejection', (reason, promise) => {
										console.trace(`${reason}@${promise}`);
									});
									
									const path = require('path');
									const { Module } = require('module');									
									globalThis.require = Module.createRequire('%s/');

									function requireFromCode(code, filename = '') {
									  const mod = new Module(filename, module.parent);
									  mod.paths = Module._nodeModulePaths(path.dirname(filename));
									  mod._compile(code, filename);
									  return mod.exports;
									}
									globalThis.requireFromCode = requireFromCode;
									
									async function loadESMModule(Source, FilePath){
										let pathToFileURL = require('node:url').pathToFileURL;
										let cascadedLoader = require('internal/modules/esm/loader').getOrInitializeCascadedLoader();
										let mod = await cascadedLoader.eval(Source, pathToFileURL(FilePath).href, true);
										return mod;
									}
									globalThis.loadESMModule = loadESMModule;
								)", (LPCSTR)ScriptRoot);
				}

				v8::TryCatch tryCatch(isolate);

				loadenv_ret = node::LoadEnvironment(env, (LPCSTR)InitScript);

				if (tryCatch.HasCaught())
				{
					v8::String::Utf8Value exception_str(isolate, tryCatch.StackTrace(context).ToLocalChecked());
					LogJSVM("%s", *exception_str);
				}

				if (loadenv_ret.IsEmpty())
				{
					jsvm_vm_dispose(pVM);
					LogJSVM("LoadEnvironment failed");
				}
				else
				{

					//isolate->SetHostImportModuleDynamicallyCallback(ImportModuleDynamically);
					//context->Global()->SetPrivate(context, env->host_defined_option_symbol(), pVM->setup->isolate_data()->vm_dynamic_import_main_context_default());
					return pVM;
				}
			}
			else
			{
				for (const std::string& err : errors)
					LogJSVM("%s\n", err.c_str());
			}
		}
		return NULL;
	}
	bool jsvm_vm_dispose(jsvm_vm* vm)
	{
		jsvm_vm_impl* pVM = (jsvm_vm_impl*)vm;
		if (pVM)
			return g_jsvm_platform.FreeVM(pVM);
		return false;
	}

	jsvm_handle_scope* jsvm_create_handle_scope(jsvm_vm* vm)
	{
		jsvm_vm_impl* pVM = (jsvm_vm_impl*)vm;
		if (pVM)
		{
			jsvm_handle_scope* scope = pVM->CreateScope();
			return scope;
		}
		return NULL;
	}
	jsvm_context* jsvm_get_context(jsvm_handle_scope* scope)
	{
		jsvm_handle_scope_impl* scope_impl = (jsvm_handle_scope_impl*)scope;
		if (scope_impl)
		{
			return v8_local_context_to_jsvm_context(scope_impl->content);
		}
		return NULL;
	}
	bool jsvm_release_handle_scope(jsvm_handle_scope* scope)
	{
		jsvm_handle_scope_impl* scope_impl = (jsvm_handle_scope_impl*)scope;
		if (scope_impl)
		{
			scope_impl->vm->ReleaseScope(scope_impl);
			return true;
		}
		return false;
	}



	jsvm_value* jsvm_run_script(jsvm_handle_scope* scope, LPCSTR script, size_t script_len, LPCSTR origin, LPCSTR source_map_url)
	{
		jsvm_handle_scope_impl* scope_impl = (jsvm_handle_scope_impl*)scope;
		if (scope_impl)
		{
			v8::Local<v8::String> ScriptStr =
				v8::String::NewFromUtf8(scope_impl->isolate, script, v8::NewStringType::kNormal, script_len ? (int)script_len : -1).ToLocalChecked();
			std::unique_ptr<v8::ScriptOrigin> Origin;
			if (origin)
			{
				v8::Local<v8::String> OriginStr =
					v8::String::NewFromUtf8(scope_impl->isolate, origin, v8::NewStringType::kNormal).ToLocalChecked();
				v8::Local<v8::String> SourceMapURL;
				if (source_map_url)
				{
					SourceMapURL = v8::String::NewFromUtf8(scope_impl->isolate, source_map_url, v8::NewStringType::kNormal).ToLocalChecked();
				}
				Origin.reset(new v8::ScriptOrigin(OriginStr, 0, 0, false, -1, SourceMapURL));
			}

			v8::MaybeLocal<v8::Script> script = v8::Script::Compile(scope_impl->content, ScriptStr, Origin.get());
			if (!script.IsEmpty())
			{
				v8::MaybeLocal<v8::Value> Result = script.ToLocalChecked()->Run(scope_impl->content);
				ProcessException(scope_impl);
				return Result.IsEmpty() ? NULL : (jsvm_value*)(*(Result.ToLocalChecked()));
			}
			else
			{
				ProcessException(scope_impl);
			}
		}
		return NULL;
	}

	jsvm_value* jsvm_load_module(jsvm_handle_scope* scope, LPCSTR script, size_t script_len, LPCSTR origin, LPCSTR source_map_url)
	{
		jsvm_handle_scope_impl* scope_impl = (jsvm_handle_scope_impl*)scope;
		if (scope_impl)
		{
			v8::Isolate* isolate = scope_impl->isolate;
			v8::Local<v8::String> ScriptStr =
				v8::String::NewFromUtf8(isolate, script, v8::NewStringType::kNormal, script_len ? (int)script_len : -1).ToLocalChecked();

			if (origin == NULL)
				origin = "unknow";
			v8::Local<v8::String> OriginStr =
				v8::String::NewFromUtf8(isolate, origin, v8::NewStringType::kNormal).ToLocalChecked();
			v8::Local<v8::String> SourceMapURL;
			if (source_map_url)
			{
				SourceMapURL = v8::String::NewFromUtf8(isolate, source_map_url, v8::NewStringType::kNormal).ToLocalChecked();
			}

			v8::ScriptOrigin Origin(OriginStr, 0, 0, false, -1, SourceMapURL, false, false, true);
			v8::ScriptCompiler::Source source(ScriptStr, Origin);
			v8::Local<v8::Module> Module = v8::ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
			if (!Module.IsEmpty())
			{
				if (!Module->InstantiateModule(scope_impl->content, ResolveModuleCallback).IsNothing())
				{
					v8::MaybeLocal<v8::Value> Result = Module->Evaluate(scope_impl->content);
					ProcessException(scope_impl);
					return Result.IsEmpty() ? NULL : (jsvm_value*)(*(Result.ToLocalChecked()));
				}
			}
			ProcessException(scope_impl);
		}
		return NULL;
	}

	jsvm_value* jsvm_call(jsvm_context* context, jsvm_value* func, jsvm_value* this_obj, jsvm_value** args, int argc)
	{
		if (context && func)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::Local<v8::Value> This = v8::Undefined(ctx->GetIsolate());
			if (this_obj)
				This = jsvm_value_to_v8_local_value(this_obj);
			auto ret = ((v8::Function*)func)->Call(ctx, This, argc, (v8::Local<v8::Value>*)args);
			if (!ret.IsEmpty())
				return (jsvm_value*)*(ret.ToLocalChecked());
		}
		return NULL;
	}

	int jsvm_event_loop(jsvm_handle_scope* scope, int max_loop_count)
	{
		jsvm_handle_scope_impl* scope_impl = (jsvm_handle_scope_impl*)scope;
		if (scope_impl)
		{
			node::Environment* env = scope_impl->vm->setup->env();
			uv_loop_s* loop = scope_impl->vm->setup->event_loop();
			env->CheckUnsettledTopLevelAwait().ToChecked();
			int ProcessCount = 0;
			int More = 0;
			do {
				More = uv_run(loop, UV_RUN_NOWAIT);
				ProcessException(scope_impl);
				max_loop_count--;
				if (More)
				{
					//在还有事件需要处理的情况下，获取下一个事件需要等待的时间，如果为0，代表需要立即执行，否则跳出循环
					if (uv_backend_timeout(loop) > 0)
						break;
				}
				if (max_loop_count == 0)
					ProcessCount++;
			} while (max_loop_count > 0 && More);
			g_jsvm_platform.platform->DrainTasks(scope_impl->isolate);
			ProcessException(scope_impl);
			if (!More)
			{
				node::EmitProcessBeforeExit(env);
				ProcessException(scope_impl);
				More = uv_loop_alive(loop);
				if (More)
				{
					if (uv_backend_timeout(loop) == 0)
						ProcessCount++;
				}
			}
			//这里返回值不代表处理次数，只代表还有事件需要立即处理
			return ProcessCount;
		}
		return 0;
	}

	jsvm_persistent_value* jsvm_create_persistent_value(jsvm_context* context, jsvm_value* value)
	{
		if (context && value)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return new jsvm_persistent_value_impl(ctx->GetIsolate(), jsvm_value_to_v8_local_value(value));
		}
		return NULL;
	}
	jsvm_value* jsvm_get_value_from_persistent_value(jsvm_context* context, jsvm_persistent_value* value)
	{
		jsvm_persistent_value_impl* value_impl = (jsvm_persistent_value_impl*)value;
		v8::Context* ctx = (v8::Context*)context;
		if (value_impl && ctx)
		{
			return (jsvm_value*)(*value_impl->value_persistent.Get(ctx->GetIsolate()));
		}
		return NULL;
	}
	bool jsvm_release_persistent_value(jsvm_persistent_value* value)
	{
		jsvm_persistent_value_impl* value_impl = (jsvm_persistent_value_impl*)value;
		if (value_impl)
		{
			delete value_impl;
			return true;
		}
		return false;
	}

	jsvm_value* jsvm_create_null(jsvm_context* context)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return v8_local_value_to_jsvm_value(v8::Null(ctx->GetIsolate()));
		}
		return NULL;
	}
	jsvm_value* jsvm_create_undefined(jsvm_context* context)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return (jsvm_value*)(*v8::Undefined(ctx->GetIsolate()));
		}
		return NULL;
	}
	jsvm_value* jsvm_create_boolean(jsvm_context* context, bool value)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return (jsvm_value*)(*v8::Boolean::New(ctx->GetIsolate(), value));
		}
		return NULL;
	}
	jsvm_value* jsvm_create_int32(jsvm_context* context, int value)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return (jsvm_value*)(*v8::Integer::New(ctx->GetIsolate(), value));
		}
		return NULL;
	}
	jsvm_value* jsvm_create_uint32(jsvm_context* context, unsigned int value)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return (jsvm_value*)(*v8::Integer::NewFromUnsigned(ctx->GetIsolate(), value));
		}
		return NULL;
	}
	jsvm_value* jsvm_create_int64(jsvm_context* context, INT64 value)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return (jsvm_value*)(*v8::BigInt::New(ctx->GetIsolate(), value));
		}
		return NULL;
	}
	jsvm_value* jsvm_create_uint64(jsvm_context* context, UINT64 value)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return (jsvm_value*)(*v8::BigInt::NewFromUnsigned(ctx->GetIsolate(), value));
		}
		return NULL;
	}
	jsvm_value* jsvm_create_double(jsvm_context* context, double value)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return (jsvm_value*)(*v8::Number::New(ctx->GetIsolate(), value));
		}
		return NULL;
	}
	jsvm_value* jsvm_create_string_utf8(jsvm_context* context, const char* str, size_t length)
	{
		if (context && str)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return (jsvm_value*)(*v8::String::NewFromUtf8(ctx->GetIsolate(), str, v8::NewStringType::kNormal, length ? (int)length : -1).ToLocalChecked());
		}
		return NULL;
	}
	jsvm_value* jsvm_create_string_gbk(jsvm_context* context, const char* str, size_t length)
	{
		if (context && str)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			size_t UCS2Len = AnsiToUnicode(str, length, NULL, 0);
			if (UCS2Len <= 4096)
			{
				uint16_t TransBuffer[4096];
				AnsiToUnicode(str, length, (WCHAR*)TransBuffer, 4096);
				return (jsvm_value*)(*v8::String::NewFromTwoByte(ctx->GetIsolate(), (const uint16_t*)TransBuffer, v8::NewStringType::kNormal, (int)UCS2Len).ToLocalChecked());
			}
			else
			{
				auto TransBuffer = v8::ArrayBuffer::New(ctx->GetIsolate(), UCS2Len * sizeof(uint16_t));
				uint16_t* pTransBuffer = (uint16_t*)TransBuffer->GetBackingStore()->Data();
				AnsiToUnicode(str, length, (WCHAR*)pTransBuffer, 4096);
				return (jsvm_value*)(*v8::String::NewFromTwoByte(ctx->GetIsolate(), (const uint16_t*)pTransBuffer, v8::NewStringType::kNormal, (int)UCS2Len).ToLocalChecked());
			}
		}
		return NULL;
	}
	jsvm_value* jsvm_create_string_utf16(jsvm_context* context, const WCHAR* str, size_t length)
	{
		if (context && str)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return (jsvm_value*)(*v8::String::NewFromTwoByte(ctx->GetIsolate(), (const uint16_t*)str, v8::NewStringType::kNormal, length ? (int)length : -1).ToLocalChecked());
		}
		return NULL;
	}
	jsvm_value* jsvm_create_binary_empty(jsvm_context* context, size_t length)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return (jsvm_value*)(*v8::ArrayBuffer::New(ctx->GetIsolate(), length));
		}
		return NULL;
	}
	jsvm_value* jsvm_create_binary(jsvm_context* context, void* data, size_t length)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			if (data)
			{
				auto Backing = v8::ArrayBuffer::NewBackingStore(data, length, v8::BackingStore::EmptyDeleter, nullptr);
				return (jsvm_value*)(*v8::ArrayBuffer::New(ctx->GetIsolate(), std::move(Backing)));
			}
			else
			{
				return (jsvm_value*)(*v8::ArrayBuffer::New(ctx->GetIsolate(), length));
			}
		}
		return NULL;
	}
	jsvm_value* jsvm_create_binary_copy_data(jsvm_context* context, const void* data, size_t length)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			if (data)
			{
				auto Backing = v8::ArrayBuffer::NewBackingStore(ctx->GetIsolate(), length);
				memcpy_s(Backing->Data(), length, data, length);
				return (jsvm_value*)(*v8::ArrayBuffer::New(ctx->GetIsolate(), std::move(Backing)));
			}
			else
			{
				return (jsvm_value*)(*v8::ArrayBuffer::New(ctx->GetIsolate(), length));
			}
		}
		return NULL;
	}
	jsvm_value* jsvm_create_array(jsvm_context* context)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return (jsvm_value*)(*v8::Array::New(ctx->GetIsolate()));
		}
		return NULL;
	}
	jsvm_value* jsvm_create_func(jsvm_context* context, jsvm_tmpl* tmpl)
	{
		v8::FunctionTemplate* func_templ = (v8::FunctionTemplate*)tmpl;
		if (context && func_templ && func_templ->IsFunctionTemplate())
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto func = func_templ->GetFunction(ctx);
			if (!func.IsEmpty())
				return (jsvm_value*)(*(func.ToLocalChecked()));
		}
		return NULL;
	}
	jsvm_value* jsvm_create_obj(jsvm_context* context)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return (jsvm_value*)(*v8::Object::New(ctx->GetIsolate()));
		}
		return NULL;
	}
	jsvm_value* jsvm_new_obj(jsvm_context* context, jsvm_value* class_func, jsvm_value** args, int argc)
	{
		if (context && class_func && ((v8::Value*)class_func)->IsFunction())
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::Local<v8::Function> Class = jsvm_value_to_v8_local_value(class_func).As<v8::Function>();
			auto Obj = Class->NewInstance(ctx, argc, (v8::Local<v8::Value>*)args);
			if (!Obj.IsEmpty())
				return (jsvm_value*)(*Obj.ToLocalChecked());
		}
		return NULL;
	}


	bool jsvm_get_value_bool(jsvm_context* context, jsvm_value* value)
	{
		if (context && value)
		{
			return reinterpret_cast<v8::Value*>(value)->BooleanValue(reinterpret_cast<v8::Context*>(context)->GetIsolate());
		}
		return false;
	}
	int jsvm_get_value_int32(jsvm_context* context, jsvm_value* value)
	{
		if (context && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto Value = (reinterpret_cast<v8::Value*>(value)->Int32Value(ctx));
			if (!Value.IsNothing())
				return Value.ToChecked();
		}
		return 0;
	}
	unsigned int jsvm_get_value_uint32(jsvm_context* context, jsvm_value* value)
	{
		if (context && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto Value = (reinterpret_cast<v8::Value*>(value)->Uint32Value(ctx));
			if (!Value.IsNothing())
				return Value.ToChecked();
		}
		return 0;
	}
	INT64 jsvm_get_value_int64(jsvm_context* context, jsvm_value* value)
	{
		if (context && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto Value = (reinterpret_cast<v8::Value*>(value)->ToBigInt(ctx));
			if (!Value.IsEmpty())
				return Value.ToLocalChecked()->Int64Value();
		}
		return 0;
	}
	UINT64 jsvm_get_value_uint64(jsvm_context* context, jsvm_value* value)
	{
		if (context && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto Value = (reinterpret_cast<v8::Value*>(value)->ToBigInt(ctx));
			if (!Value.IsEmpty())
				return Value.ToLocalChecked()->Uint64Value();
		}
		return 0;
	}
	double jsvm_get_value_double(jsvm_context* context, jsvm_value* value)
	{
		if (context && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto Value = (reinterpret_cast<v8::Value*>(value)->NumberValue(ctx));
			if (!Value.IsNothing())
				return Value.ToChecked();
		}
		return 0;
	}
	int jsvm_get_value_string_utf8(jsvm_context* context, jsvm_value* value, char* buf, int bufsize)
	{
		if (context && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto str = reinterpret_cast<v8::Value*>(value)->ToString(ctx);
			if (!str.IsEmpty())
			{
				if (buf)
				{
					int WriteLen = 0;
					str.ToLocalChecked()->WriteUtf8(ctx->GetIsolate(), buf, bufsize - 1, &WriteLen);
					buf[WriteLen] = 0;
					return WriteLen;
				}
				else
				{
					return str.ToLocalChecked()->Utf8Length(ctx->GetIsolate());
				}
			}
		}
		return -1;
	}
	const char* jsvm_get_value_string_utf8_no_buff(jsvm_context* context, jsvm_value* value)
	{
		if (context && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto Str = reinterpret_cast<v8::Value*>(value)->ToString(ctx);
			if (!Str.IsEmpty())
			{
				auto str = Str.ToLocalChecked();
				int len = str->Utf8Length(ctx->GetIsolate());
				if (len <= 0)
				{
					return CEasyStringA::EMPTY_PSTR;
				}
				else
				{
					auto buff = v8::ArrayBuffer::New(ctx->GetIsolate(), len + 1);
					char* pBuff = (char*)buff->GetBackingStore()->Data();
					str->WriteUtf8(ctx->GetIsolate(), pBuff, len);
					return pBuff;
				}
			}
		}
		return NULL;
	}
	int jsvm_get_value_string_gbk(jsvm_context* context, jsvm_value* value, char* buf, int bufsize)
	{
		if (context && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto Str = reinterpret_cast<v8::Value*>(value)->ToString(ctx);
			if (!Str.IsEmpty())
			{
				auto str = Str.ToLocalChecked();
				v8::String::Value StrValue(ctx->GetIsolate(), str);
				int len = StrValue.length();
				if (len <= 0)
				{
					if (buf)
						buf[0] = 0;
					return 0;
				}
				else if (len <= 4096)
				{
					uint16_t TransBuffer[4096];
					len = str->Write(ctx->GetIsolate(), TransBuffer, 0, len);
					size_t GBKLen = UnicodeToAnsi((const WCHAR*)TransBuffer, len, buf, bufsize);
					return (int)GBKLen;
				}
				else
				{
					auto TransBuffer = v8::ArrayBuffer::New(ctx->GetIsolate(), len * sizeof(uint16_t));
					uint16_t* pTransBuffer = (uint16_t*)TransBuffer->GetBackingStore()->Data();
					len = str->Write(ctx->GetIsolate(), pTransBuffer, 0, len);
					size_t GBKLen = UnicodeToAnsi((const WCHAR*)pTransBuffer, len, buf, bufsize);
					return (int)GBKLen;
				}
			}
		}
		return -1;
	}
	const char* jsvm_get_value_string_gbk_no_buff(jsvm_context* context, jsvm_value* value)
	{
		if (context && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto Str = reinterpret_cast<v8::Value*>(value)->ToString(ctx);
			if (!Str.IsEmpty())
			{
				auto str = Str.ToLocalChecked();
				v8::String::Value StrValue(ctx->GetIsolate(), str);
				int len = StrValue.length();
				if (len <= 0)
				{
					return CEasyStringA::EMPTY_PSTR;
				}
				else if (len < 4096)
				{
					uint16_t TransBuffer[4096];
					len = str->Write(ctx->GetIsolate(), TransBuffer, 0, len);
					size_t GBKLen = UnicodeToAnsi((const WCHAR*)TransBuffer, len, NULL, 0);
					auto buff = v8::ArrayBuffer::New(ctx->GetIsolate(), GBKLen + 1);
					char* pBuff = (char*)buff->GetBackingStore()->Data();
					UnicodeToAnsi((const WCHAR*)TransBuffer, len, pBuff, GBKLen);
					pBuff[GBKLen] = 0;
					return (const char*)pBuff;
				}
				else
				{
					auto TransBuffer = v8::ArrayBuffer::New(ctx->GetIsolate(), len * sizeof(uint16_t));
					uint16_t* pTransBuffer = (uint16_t*)TransBuffer->GetBackingStore()->Data();
					len = str->Write(ctx->GetIsolate(), pTransBuffer, 0, len);
					size_t GBKLen = UnicodeToAnsi((const WCHAR*)pTransBuffer, len, NULL, 0);
					auto buff = v8::ArrayBuffer::New(ctx->GetIsolate(), GBKLen + 1);
					char* pBuff = (char*)buff->GetBackingStore()->Data();
					UnicodeToAnsi((const WCHAR*)pTransBuffer, len, pBuff, GBKLen);
					pBuff[GBKLen] = 0;
					return (const char*)pBuff;
				}
			}
		}
		return NULL;
	}
	int jsvm_get_value_string_utf16(jsvm_context* context, jsvm_value* value, WCHAR* buf, int bufsize)
	{
		if (context && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto str = reinterpret_cast<v8::Value*>(value)->ToString(ctx);
			if (!str.IsEmpty())
			{
				if (buf)
				{
					int WriteLen = str.ToLocalChecked()->Write(ctx->GetIsolate(), (uint16_t*)buf, 0, bufsize - 1);
					buf[WriteLen] = 0;
					return WriteLen;
				}
				else
				{
					return str.ToLocalChecked()->Length();
				}
			}
		}
		return -1;
	}
	const WCHAR* jsvm_get_value_string_utf16_no_buff(jsvm_context* context, jsvm_value* value)
	{
		if (context && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto Str = reinterpret_cast<v8::Value*>(value)->ToString(ctx);
			if (!Str.IsEmpty())
			{
				auto str = Str.ToLocalChecked();
				int len = str->Length();
				if (len <= 0)
				{
					return CEasyStringW::EMPTY_PSTR;
				}
				else
				{
					auto buff = v8::ArrayBuffer::New(ctx->GetIsolate(), (len + 1) * sizeof(uint16_t));
					uint16_t* pBuff = (uint16_t*)buff->GetBackingStore()->Data();
					str->Write(ctx->GetIsolate(), pBuff, 0, len);
					pBuff[len] = 0;
					return (const WCHAR*)pBuff;
				}
			}
		}
		return NULL;
	}
	void* jsvm_get_value_binary(jsvm_context* context, jsvm_value* value, size_t* len)
	{
		if (context && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::Local<v8::Value> val = jsvm_value_to_v8_local_value(value);
			if (val->IsArrayBufferView())
			{
				v8::Local<v8::ArrayBufferView> buffView = val.As<v8::ArrayBufferView>();
				if (len)
					*len = buffView->ByteLength();
				auto ab = buffView->Buffer();
				auto bs = ab->GetBackingStore();
				return static_cast<char*>(bs->Data()) + buffView->ByteOffset();
			}
			if (val->IsArrayBuffer())
			{
				auto ab = v8::Local<v8::ArrayBuffer>::Cast(val);
				auto bs = ab->GetBackingStore();
				if (len)
					*len = bs->ByteLength();
				return (bs->Data());
			}
		}
		return NULL;
	}
	unsigned int jsvm_get_array_length(jsvm_context* context, jsvm_value* value)
	{
		if (context && value)
		{
			v8::Local<v8::Value> val = jsvm_value_to_v8_local_value(value);
			if (val->IsArray())
			{
				return val.As<v8::Array>()->Length();
			}
		}
		return 0;
	}

	int jsvm_get_type(jsvm_context* context, jsvm_value* value, char* buff, int bufsize)
	{
		if (context && value)
		{
			v8::Context* ctx = (v8::Context*)context;
			auto Type = ((v8::Value*)value)->TypeOf(ctx->GetIsolate());
			if (!Type.IsEmpty())
			{
				if (buff)
				{
					int WriteLen = 0;
					Type->WriteUtf8(ctx->GetIsolate(), buff, bufsize - 1, &WriteLen);
					buff[bufsize - 1] = 0;
					return WriteLen;
				}
				else
				{
					return Type->Utf8Length(ctx->GetIsolate());
				}
			}
		}
		return -1;
	}
	const char* jsvm_get_type_no_buff(jsvm_context* context, jsvm_value* value)
	{
		if (context && value)
		{
			v8::Context* ctx = (v8::Context*)context;
			auto Type = ((v8::Value*)value)->TypeOf(ctx->GetIsolate());
			if (!Type.IsEmpty())
			{
				int len = Type->Utf8Length(ctx->GetIsolate());
				auto buff = v8::ArrayBuffer::New(ctx->GetIsolate(), len + 1);
				char* pBuff = (char*)buff->GetBackingStore()->Data();
				Type->WriteUtf8(ctx->GetIsolate(), pBuff, len);
				return pBuff;
			}
		}
		return NULL;
	}
	bool jsvm_is_null(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsNull();
		return false;
	}
	bool jsvm_is_undefined(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsUndefined();
		return false;
	}
	bool jsvm_is_boolean(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsBoolean();
		return false;
	}
	bool jsvm_is_int32(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsInt32();
		return false;
	}
	bool jsvm_is_uint32(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsUint32();
		return false;
	}
	bool jsvm_is_int64(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsBigInt();
		return false;
	}
	bool jsvm_is_uint64(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsBigInt();
		return false;
	}
	bool jsvm_is_double(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsNumber();
		return false;
	}
	bool jsvm_is_string(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsString();
		return false;
	}
	bool jsvm_is_object(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsObject();
		return false;
	}
	bool jsvm_is_function(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsFunction();
		return false;
	}
	bool jsvm_is_binary(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsArrayBuffer() || reinterpret_cast<v8::Value*>(value)->IsArrayBufferView();
		return false;
	}
	bool jsvm_is_array(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsArray();
		return false;
	}
	bool jsvm_is_promise(jsvm_value* value)
	{
		if (value)
			return reinterpret_cast<v8::Value*>(value)->IsPromise();
		return false;
	}
	bool jsvm_is_class(jsvm_context* context, jsvm_value* value, const char* class_name)
	{
		v8::Function* cst = (v8::Function*)value;
		if (context && cst && cst->IsFunction() && class_name)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto TargetName = v8::String::NewFromUtf8(ctx->GetIsolate(), class_name).ToLocalChecked();
			auto FuncName = cst->GetName().As<v8::String>();
			return FuncName->Equals(ctx, TargetName).ToChecked();
		}
		return false;
	}
	bool jsvm_of_class(jsvm_context* context, jsvm_value* value, const char* class_name)
	{
		v8::Object* obj = (v8::Object*)value;
		if (context && obj && class_name)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto TargetName = v8::String::NewFromUtf8(ctx->GetIsolate(), class_name).ToLocalChecked();
			do {
				auto ObjName = obj->GetConstructorName();
				if (ObjName->Equals(ctx, TargetName).ToChecked())
					return true;
				obj = *obj->GetPrototype().As<v8::Object>();
			} while (!obj->IsNull() && !obj->IsUndefined());

		}
		return false;
	}

	int jsvm_value_serialize(jsvm_context* context, jsvm_value** values, int value_count, CEasyBuffer& Buffer)
	{
		if (context && values && value_count > 0)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::ValueSerializer Serializer(ctx->GetIsolate());
			Serializer.WriteHeader();
			int serialized_count = 0;
			for (int i = 0; i < value_count; i++)
			{
				v8::Local<v8::Value> val = jsvm_value_to_v8_local_value(values[i]);
				auto ret = Serializer.WriteValue(ctx, val);
				if (!ret.IsNothing())
				{
					if (ret.ToChecked())
						serialized_count++;
				}
			}
			if (serialized_count)
			{
				auto Data = Serializer.Release();
				Buffer.Create(Data.second);
				Buffer.PushBack(Data.first, Data.second);
				return serialized_count;
			}
		}
		return 0;
	}
	int jsvm_value_deserialize(jsvm_context* context, const void* data, size_t data_len, jsvm_value** values, int value_count)
	{
		if (context && data && data_len && values && value_count > 0)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::ValueDeserializer Deserializer(ctx->GetIsolate(), (const uint8_t*)data, data_len);
			if (Deserializer.ReadHeader(ctx).ToChecked())
			{
				int deserialize_count = 0;
				for (int i = 0; i < value_count && deserialize_count < value_count; i++)
				{
					auto Value = Deserializer.ReadValue(ctx);
					if (!Value.IsEmpty())
					{
						values[deserialize_count] = (jsvm_value*)(*Value.ToLocalChecked());;
						deserialize_count++;
					}
				}
				return deserialize_count;
			}
		}
		return 0;
	}

	jsvm_tmpl* jsvm_create_func_tmpl(jsvm_context* context, jsvm_callback func, jsvm_func_type func_type, void* pass_data, const char* class_name)
	{
		v8::Context* ctx = (v8::Context*)context;
		if (ctx)
		{
			v8::Isolate* isolate = ctx->GetIsolate();
			v8::Local<v8::Value> Data;
			if (pass_data)
				Data = v8::External::New(isolate, pass_data);
			v8::Local<v8::FunctionTemplate> func_tmpl = v8::FunctionTemplate::New(isolate, (v8::FunctionCallback)func, Data,
				v8::Local<v8::Signature>(), 0, func_type == jsvm_func_constructor ? v8::ConstructorBehavior::kAllow : v8::ConstructorBehavior::kThrow);

			if (class_name)
				func_tmpl->SetClassName(v8::String::NewFromUtf8(isolate, class_name).ToLocalChecked());
			return (jsvm_tmpl*)(*func_tmpl);
		}
		return NULL;
	}
	jsvm_persistent_tmpl* jsvm_create_persistent_tmpl(jsvm_context* context, jsvm_tmpl* tmpl)
	{
		if (context && tmpl)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			return new jsvm_persistent_tmpl_impl(ctx->GetIsolate(), jsvm_tmpl_to_v8_local_tmpl(tmpl));
		}
		return NULL;
	}
	jsvm_tmpl* jsvm_get_tmpl_from_persistent_tmpl(jsvm_context* context, jsvm_persistent_tmpl* tmpl)
	{
		jsvm_persistent_tmpl_impl* tmpl_impl = (jsvm_persistent_tmpl_impl*)tmpl;
		v8::Context* ctx = (v8::Context*)context;
		if (tmpl_impl && ctx)
		{
			return (jsvm_tmpl*)(*tmpl_impl->tmpl_persistent.Get(ctx->GetIsolate()));
		}
		return NULL;
	}
	bool jsvm_release_persistent_tmpl(jsvm_context* context, jsvm_persistent_tmpl* tmpl)
	{
		jsvm_persistent_tmpl_impl* tmpl_impl = (jsvm_persistent_tmpl_impl*)tmpl;
		if (tmpl_impl)
		{
			delete tmpl_impl;
			return true;
		}
		return false;
	}

	bool jsvm_class_set_internal_field_count(jsvm_tmpl* constructor, int field_count)
	{
		v8::FunctionTemplate* cst = (v8::FunctionTemplate*)constructor;
		if (cst && cst->IsFunctionTemplate())
		{
			cst->InstanceTemplate()->SetInternalFieldCount(field_count);
			return true;
		}
		return false;
	}
	bool jsvm_class_set_super_class(jsvm_tmpl* constructor, jsvm_tmpl* super_constructor)
	{
		v8::FunctionTemplate* cst = (v8::FunctionTemplate*)constructor;
		if (cst && cst->IsFunctionTemplate() && super_constructor && ((v8::Template*)super_constructor)->IsFunctionTemplate())
		{
			v8::Local<v8::FunctionTemplate> super = jsvm_tmpl_to_v8_local_func_tmpl(super_constructor);
			cst->Inherit(super);
			return true;
		}
		return false;
	}
	bool jsvm_class_set_method(jsvm_context* context, jsvm_tmpl* constructor, const char* method_name, jsvm_tmpl* method, bool is_static)
	{
		v8::Context* ctx = (v8::Context*)context;
		v8::FunctionTemplate* cst = (v8::FunctionTemplate*)constructor;
		if (ctx && cst && cst->IsFunctionTemplate() && method && ((v8::Template*)method)->IsFunctionTemplate())
		{
			v8::Isolate* isolate = ctx->GetIsolate();
			v8::Local<v8::String> Name = v8::String::NewFromUtf8(isolate, method_name).ToLocalChecked();
			v8::Local<v8::FunctionTemplate> md = jsvm_tmpl_to_v8_local_func_tmpl(method);
			if (is_static)
				cst->Set(Name, md);
			else
				cst->PrototypeTemplate()->Set(Name, md);
			return true;
		}
		return false;
	}
	bool jsvm_class_set_property(jsvm_context* context, jsvm_tmpl* constructor, const char* prop_name, jsvm_tmpl* getter, jsvm_tmpl* setter, bool is_static)
	{
		v8::Context* ctx = (v8::Context*)context;
		v8::FunctionTemplate* cst = (v8::FunctionTemplate*)constructor;
		if (ctx && cst && cst->IsFunctionTemplate() &&
			(getter || setter) &&
			(getter == NULL || ((v8::Template*)getter)->IsFunctionTemplate()) &&
			(setter == NULL || ((v8::Template*)setter)->IsFunctionTemplate()))
		{
			v8::Isolate* isolate = ctx->GetIsolate();
			v8::Local<v8::String> Name = v8::String::NewFromUtf8(isolate, prop_name).ToLocalChecked();
			v8::Local<v8::FunctionTemplate> get;
			if (getter)
				get = jsvm_tmpl_to_v8_local_func_tmpl(getter);
			v8::Local<v8::FunctionTemplate> set;
			if (setter)
				set = jsvm_tmpl_to_v8_local_func_tmpl(setter);
			if (is_static)
				cst->SetAccessorProperty(Name, get, set);
			else
				cst->PrototypeTemplate()->SetAccessorProperty(Name, get, set);
			return true;
		}
		return false;
	}
	bool jsvm_class_set_member(jsvm_context* context, jsvm_tmpl* constructor, const char* member_name, jsvm_value* member, bool is_static)
	{
		v8::Context* ctx = (v8::Context*)context;
		v8::FunctionTemplate* cst = (v8::FunctionTemplate*)constructor;
		if (ctx && cst && cst->IsFunctionTemplate() && member)
		{
			v8::Isolate* isolate = ctx->GetIsolate();
			v8::Local<v8::String> Name = v8::String::NewFromUtf8(isolate, member_name).ToLocalChecked();
			v8::Local<v8::Value> Member = jsvm_value_to_v8_local_value(member);
			if (is_static)
				cst->Set(Name, Member);
			else
				cst->PrototypeTemplate()->Set(Name, Member);
			return true;
		}
		return false;
	}

	jsvm_value* jsvm_global(jsvm_context* context)
	{
		v8::Context* ctx = (v8::Context*)context;
		if (ctx)
			return (jsvm_value*)(*ctx->Global());
		return NULL;
	}
	bool jsvm_global_set(jsvm_context* context, const char* member_name, jsvm_value* value)
	{
		if (context && member_name && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::Isolate* isolate = ctx->GetIsolate();
			v8::Local<v8::String> Name = v8::String::NewFromUtf8(isolate, member_name).ToLocalChecked();
			v8::Local<v8::Value> Value = jsvm_value_to_v8_local_value(value);
			return ctx->Global()->Set(ctx, Name, Value).FromJust();
		}
		return false;
	}
	jsvm_value* jsvm_global_get(jsvm_context* context, const char* member_name)
	{
		if (context && member_name)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::Isolate* isolate = ctx->GetIsolate();
			v8::Local<v8::String> Name = v8::String::NewFromUtf8(isolate, member_name).ToLocalChecked();
			auto Value = ctx->Global()->Get(ctx, Name);
			if (!Value.IsEmpty())
				return (jsvm_value*)(*Value.ToLocalChecked());
		}
		return NULL;
	}
	bool jsvm_obj_set(jsvm_context* context, jsvm_value* obj, const char* member_name, jsvm_value* value)
	{
		v8::Object* pObj = (v8::Object*)obj;
		if (context && pObj && pObj->IsObject() && member_name && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::Isolate* isolate = ctx->GetIsolate();
			v8::Local<v8::String> Name = v8::String::NewFromUtf8(isolate, member_name).ToLocalChecked();
			v8::Local<v8::Value> Value = jsvm_value_to_v8_local_value(value);
			return pObj->Set(ctx, Name, Value).FromJust();
		}
		return false;
	}
	jsvm_value* jsvm_obj_get(jsvm_context* context, jsvm_value* obj, const char* member_name)
	{
		v8::Object* pObj = (v8::Object*)obj;
		if (context && pObj && pObj->IsObject() && member_name)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::Isolate* isolate = ctx->GetIsolate();
			v8::Local<v8::String> Name = v8::String::NewFromUtf8(isolate, member_name).ToLocalChecked();
			auto Value = pObj->Get(ctx, Name);
			if (!Value.IsEmpty())
				return (jsvm_value*)(*Value.ToLocalChecked());
		}
		return NULL;
	}

	bool jsvm_obj_set_by_index(jsvm_context* context, jsvm_value* obj, UINT index, jsvm_value* value)
	{
		v8::Object* pObj = (v8::Object*)obj;
		if (context && pObj && pObj->IsObject() && value)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::Local<v8::Value> Value = jsvm_value_to_v8_local_value(value);
			return pObj->Set(ctx, index, Value).FromJust();
		}
		return false;
	}
	jsvm_value* jsvm_obj_get_by_index(jsvm_context* context, jsvm_value* obj, UINT index)
	{
		v8::Object* pObj = (v8::Object*)obj;
		if (context && pObj && pObj->IsObject())
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto Value = pObj->Get(ctx, index);
			if (!Value.IsEmpty())
				return (jsvm_value*)(*Value.ToLocalChecked());
		}
		return NULL;
	}

	bool jsvm_obj_set_internal_field(jsvm_context* context, jsvm_value* obj, int field, const void* data)
	{
		v8::Object* pObj = (v8::Object*)obj;
		if (context && pObj && pObj->IsObject() && (!pObj->IsArray()))
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::Isolate* isolate = ctx->GetIsolate();
			if (data)
				pObj->SetInternalField(field, v8::External::New(isolate, (void*)data));
			else
				pObj->SetInternalField(field, v8::Undefined(isolate));
			return true;
		}
		return false;
	}
	const void* jsvm_obj_get_internal_field(jsvm_context* context, jsvm_value* obj, int field)
	{
		v8::Object* pObj = (v8::Object*)obj;
		if (context && pObj && pObj->IsObject())
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::Isolate* isolate = ctx->GetIsolate();
			auto data = pObj->GetInternalField(field).As<v8::Value>().As<v8::External>();
			if (!data.IsEmpty() && data->IsExternal())
				return data->Value();
		}
		return NULL;
	}
	jsvm_value* jsvm_get_obj_keys(jsvm_context* context, jsvm_value* obj, bool include_prototypes)
	{
		v8::Object* pObj = (v8::Object*)obj;
		if (context && pObj && pObj->IsObject())
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto keys = pObj->GetPropertyNames(ctx, include_prototypes ? v8::KeyCollectionMode::kIncludePrototypes : v8::KeyCollectionMode::kOwnOnly,
				(v8::PropertyFilter)(v8::PropertyFilter::ONLY_ENUMERABLE | v8::PropertyFilter::SKIP_SYMBOLS), v8::IndexFilter::kIncludeIndices);
			if (!keys.IsEmpty())
				return (jsvm_value*)(*keys.ToLocalChecked());
		}
		return NULL;
	}
	jsvm_value* jsvm_get_class_name(jsvm_context* context, jsvm_value* obj)
	{
		v8::Object* pObj = (v8::Object*)obj;
		if (context && pObj && pObj->IsObject())
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto name = pObj->GetConstructorName();
			return (jsvm_value*)(*name);
		}
		return NULL;
	}
	jsvm_value* jsvm_class_get_name(jsvm_context* context, jsvm_value* constructor)
	{
		v8::Function* cst = (v8::Function*)constructor;
		if (context && cst && cst->IsFunction())
		{
			return (jsvm_value*)(*cst->GetName().As<v8::String>());
		}
		return NULL;
	}

	jsvm_value* jsvm_callback_get_this(jsvm_callback_info* callback_info)
	{
		const v8::FunctionCallbackInfo<v8::Value>* pCallbackInfo = (v8::FunctionCallbackInfo<v8::Value>*)callback_info;
		if (pCallbackInfo)
		{
			return (jsvm_value*)(*(pCallbackInfo->This()));
		}
		return NULL;
	}
	int jsvm_callback_get_arg_len(jsvm_callback_info* callback_info)
	{
		const v8::FunctionCallbackInfo<v8::Value>* pCallbackInfo = (v8::FunctionCallbackInfo<v8::Value>*)callback_info;
		if (pCallbackInfo)
		{
			return pCallbackInfo->Length();
		}
		return 0;
	}
	jsvm_value* jsvm_callback_get_arg(jsvm_callback_info* callback_info, int index)
	{
		const v8::FunctionCallbackInfo<v8::Value>* pCallbackInfo = (v8::FunctionCallbackInfo<v8::Value>*)callback_info;
		if (pCallbackInfo)
		{
			return (jsvm_value*)(*((*pCallbackInfo)[index]));
		}
		return NULL;
	}
	bool jsvm_callback_set_return(jsvm_callback_info* callback_info, jsvm_value* value)
	{
		const v8::FunctionCallbackInfo<v8::Value>* pCallbackInfo = (v8::FunctionCallbackInfo<v8::Value>*)callback_info;
		if (pCallbackInfo && value)
		{
			v8::Local<v8::Value> Value = jsvm_value_to_v8_local_value(value);
			pCallbackInfo->GetReturnValue().Set(Value);
			return true;
		}
		return false;
	}
	jsvm_context* jsvm_callback_get_cur_context(jsvm_callback_info* callback_info)
	{
		const v8::FunctionCallbackInfo<v8::Value>* pCallbackInfo = (v8::FunctionCallbackInfo<v8::Value>*)callback_info;
		if (pCallbackInfo)
		{
			return (jsvm_context*)*(pCallbackInfo->GetIsolate()->GetCurrentContext());
		}
		return NULL;
	}
	void* jsvm_callback_get_pass_data(jsvm_callback_info* callback_info)
	{
		const v8::FunctionCallbackInfo<v8::Value>* pCallbackInfo = (v8::FunctionCallbackInfo<v8::Value>*)callback_info;
		if (pCallbackInfo)
		{
			if (pCallbackInfo->Data()->IsExternal())
				return (v8::Local<v8::External>::Cast(pCallbackInfo->Data()))->Value();
		}
		return NULL;
	}

	jsvm_value* jsvm_create_resolver(jsvm_context* context)
	{
		if (context)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			auto Resolver = v8::Promise::Resolver::New(ctx);
			if (!Resolver.IsEmpty())
				return (jsvm_value*)(*Resolver.ToLocalChecked());
		}
		return NULL;
	}
	jsvm_value* jsvm_get_promise(jsvm_value* resolver)
	{
		if (resolver)
		{
			return (jsvm_value*)(*((v8::Promise::Resolver*)resolver)->GetPromise());
		}
		return NULL;
	}
	bool jsvm_resolver_resolve(jsvm_context* context, jsvm_value* resolver, jsvm_value* return_value)
	{
		if (context && resolver)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::Local<v8::Value> Value;
			if (return_value)
				Value = jsvm_value_to_v8_local_value(return_value);
			else
				Value = v8::Undefined(ctx->GetIsolate());
			return ((v8::Promise::Resolver*)resolver)->Resolve(ctx, Value).ToChecked();
		}
		return false;
	}
	bool jsvm_resolver_reject(jsvm_context* context, jsvm_value* resolver, jsvm_value* return_value)
	{
		if (context && resolver)
		{
			v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
			v8::Local<v8::Value> Value;
			if (return_value)
				Value = jsvm_value_to_v8_local_value(return_value);
			else
				Value = v8::Undefined(ctx->GetIsolate());
			return ((v8::Promise::Resolver*)resolver)->Reject(ctx, Value).ToChecked();
		}
		return false;
	}
	bool jsvm_set_promise_then(jsvm_context* context, jsvm_value* promise, jsvm_value* callback_func)
	{
		if (context && promise && callback_func)
		{
			v8::Value* Promise = (v8::Value*)promise;
			if (((v8::Value*)promise)->IsPromise() && ((v8::Value*)callback_func)->IsFunction())
			{
				v8::Local<v8::Context> ctx = jsvm_context_to_v8_local_context(context);
				v8::Local<v8::Function> func = jsvm_ptr_to_v8_local<jsvm_value, v8::Function>(callback_func);
				((v8::Promise*)promise)->Then(ctx, func);
				return true;
			}
		}
		return false;
	}
	bool jsvm_process_exception(jsvm_handle_scope* scope)
	{
		if (scope)
			return ProcessException((jsvm_handle_scope_impl*)scope);
		return false;
	}
	bool jsvm_get_exception(jsvm_handle_scope* scope, char* msg_buff, size_t buff_len)
	{
		jsvm_handle_scope_impl* scope_impl = (jsvm_handle_scope_impl*)scope;
		if (scope_impl && scope_impl->trycatch.HasCaught())
		{
			if (msg_buff == NULL)
				return true;
			v8::Local<v8::Value> Msg;
			scope_impl->trycatch.StackTrace(scope_impl->content).ToLocal(&Msg);
			V8ValueToStr(scope_impl->isolate, Msg, msg_buff, buff_len);
			msg_buff[buff_len - 1] = 0;
			scope_impl->trycatch.Reset();
			return true;
		}
		return false;
	}
	void jsvm_throw_exception(jsvm_context* context, const char* error_msg)
	{
		if (context)
		{
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			v8::Local<v8::String> msg = v8::String::NewFromUtf8(ctx->GetIsolate(), error_msg, v8::NewStringType::kNormal).ToLocalChecked();
			ctx->GetIsolate()->ThrowError(msg);
		}
	}

	void jsvm_throw_exception_with_format(jsvm_context* context, const char* error_msg, ...)
	{
		va_list	vl;
		va_start(vl, error_msg);
		jsvm_throw_exception_with_format_vl(context, error_msg, vl);
		va_end(vl);
	}

	void jsvm_throw_exception_with_format_vl(jsvm_context* context, const char* error_msg, va_list	vl)
	{
		if (context)
		{
			char Buff[4096];
			vsprintf_s(Buff, 4096, error_msg, vl);
			Buff[4095] = 0;
			v8::Context* ctx = reinterpret_cast<v8::Context*>(context);
			v8::Local<v8::String> msg = v8::String::NewFromUtf8(ctx->GetIsolate(), Buff, v8::NewStringType::kNormal).ToLocalChecked();
			ctx->GetIsolate()->ThrowError(msg);
		}
	}

	void jsvm_heap_stat(jsvm_vm* vm, vm_heap_stat* stat_info)
	{
		jsvm_vm_impl* pVM = (jsvm_vm_impl*)vm;
		if (pVM)
		{
			v8::HeapStatistics HeapStats;
			pVM->setup->isolate()->GetHeapStatistics(&HeapStats);
			stat_info->total_heap_size = HeapStats.total_heap_size();
			stat_info->total_heap_size_executable = HeapStats.total_heap_size_executable();
			stat_info->total_physical_size = HeapStats.total_physical_size();
			stat_info->total_available_size = HeapStats.total_available_size();
			stat_info->total_global_handles_size = HeapStats.total_global_handles_size();
			stat_info->used_global_handles_size = HeapStats.used_global_handles_size();
			stat_info->used_heap_size = HeapStats.used_heap_size();
			stat_info->heap_size_limit = HeapStats.heap_size_limit();
			stat_info->malloced_memory = HeapStats.malloced_memory();
			stat_info->external_memory = HeapStats.external_memory();
			stat_info->peak_malloced_memory = HeapStats.peak_malloced_memory();
			stat_info->number_of_native_contexts = HeapStats.number_of_native_contexts();
			stat_info->number_of_detached_contexts = HeapStats.number_of_detached_contexts();
		}
	}

	bool jsvm_heap_dump(jsvm_vm* vm, const char* dump_file_name)
	{
		jsvm_vm_impl* pVM = (jsvm_vm_impl*)vm;
		if (pVM)
		{
			FileStreamForDump Stream;
			if (Stream.Open(dump_file_name))
			{
				const v8::HeapSnapshot* pShanshot = pVM->setup->isolate()->GetHeapProfiler()->TakeHeapSnapshot();
				if (pShanshot)
				{
					pShanshot->Serialize(&Stream);
				}
				return true;
			}
		}
		return false;
	}
	void jsvm_gc(jsvm_vm* vm, int level, bool full_gc)
	{
		jsvm_vm_impl* pVM = (jsvm_vm_impl*)vm;
		if (pVM)
		{
			if (level == 0)
			{
				pVM->setup->isolate()->MemoryPressureNotification(v8::MemoryPressureLevel::kModerate);
			}
			else if (level == 1)
			{
				pVM->setup->isolate()->MemoryPressureNotification(v8::MemoryPressureLevel::kCritical);
			}
			else if (level == 2)
			{
				pVM->setup->isolate()->LowMemoryNotification();
			}
			else
			{
				pVM->setup->isolate()->RequestGarbageCollectionForTesting(full_gc ? v8::Isolate::GarbageCollectionType::kFullGarbageCollection : v8::Isolate::GarbageCollectionType::kMinorGarbageCollection);
			}
		}
	}

	void jsvm_set_console_log_channel(jsvm_vm* vm, unsigned int LogChannel)
	{
		jsvm_vm_impl* pVM = (jsvm_vm_impl*)vm;
		if (pVM)
		{
			pVM->ConsoleLogChannel = LogChannel;
			pVM->setup->isolate()->SetData(ISOLATE_DATA_SLOT_LOG_CHANNEL, (void*)LogChannel);
		}
	}
}
