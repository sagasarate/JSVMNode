// JSVMTest.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "stdafx.h"

CEasyTimer WaitTimer;
jsvm_persistent_value* pResolver = NULL;

void AddTest(jsvm_callback_info* callback_info)
{
	if (jsvm_callback_get_arg_len(callback_info) >= 2)
	{
		jsvm_context* context = jsvm_callback_get_cur_context(callback_info);
		jsvm_value* pValue1 = jsvm_callback_get_arg(callback_info, 0);
		jsvm_value* pValue2 = jsvm_callback_get_arg(callback_info, 1);
		if (context && pValue1 && pValue2)
		{
			double a = jsvm_get_value_double(context, pValue1);
			double b = jsvm_get_value_double(context, pValue2);
			jsvm_value* pReturn = jsvm_create_double(context, a + b);
			jsvm_callback_set_return(callback_info, pReturn);
		}
	}

}

void Wait(jsvm_callback_info* callback_info)
{
	if (jsvm_callback_get_arg_len(callback_info) >= 1)
	{
		jsvm_context* context = jsvm_callback_get_cur_context(callback_info);
		jsvm_value* pValue = jsvm_callback_get_arg(callback_info, 0);
		if (context && pValue)
		{
			WaitTimer.SetTimeOut(jsvm_get_value_uint32(context, pValue));
			jsvm_value* resolver = jsvm_create_resolver(context);
			if (resolver)
			{
				jsvm_value* promise = jsvm_get_promise(resolver);
				if (promise)
				{
					jsvm_callback_set_return(callback_info, promise);
					pResolver = jsvm_create_persistent_value(context, resolver);
				}

			}
		}
	}
}

void OnModuleLoaded(jsvm_callback_info* callback_info)
{
	int Argc = jsvm_callback_get_arg_len(callback_info);
	if (jsvm_callback_get_arg_len(callback_info) >= 1)
	{
		jsvm_context* context = jsvm_callback_get_cur_context(callback_info);
		jsvm_value* result = jsvm_callback_get_arg(callback_info, 0);
		if (jsvm_is_object(result))
		{
			jsvm_value* module = jsvm_obj_get(context, result, "namespace");
			if (module)
			{
				jsvm_value* Keys = jsvm_get_obj_keys(context, module, true);
				if (Keys)
				{
					UINT len = jsvm_get_array_length(context, Keys);
					for (UINT i = 0; i < len; i++)
					{
						jsvm_value* Key = jsvm_obj_get_by_index(context, Keys, i);
						if (Key)
							printf("%s\n", jsvm_get_value_string_utf8_no_buff(context, Key));
					}

				}
			}
		}
	}
}
#define PLUGIN_INIT_FN_NAME						"InitPlugin"
#define PLUGIN_CHECK_RELEASE_FN_NAME			"CheckPluginRelease"
CEasyBuffer JSClassSerializeData;
void OnESMoudleLoaded(JSVM::jsvm_context* context, JSVM::jsvm_value* result)
{
	if (jsvm_is_object(result))
	{
		jsvm_value* module = jsvm_obj_get(context, result, "namespace");
		if (module)
		{
			jsvm_value* Keys = jsvm_get_obj_keys(context, module, true);
			if (Keys)
			{
				UINT len = jsvm_get_array_length(context, Keys);
				for (UINT i = 0; i < len; i++)
				{
					jsvm_value* Key = jsvm_obj_get_by_index(context, Keys, i);
					if (Key)
						printf("%s\n", jsvm_get_value_string_utf8_no_buff(context, Key));
				}

				JSVM::jsvm_value* serialize_values[4];

				JSVM::jsvm_value* dos_classes = JSVM::jsvm_obj_get(context, module, "DOS_CLASSES");
				if ((!dos_classes) || (!JSVM::jsvm_is_object(dos_classes)))
				{
					JSVM::jsvm_throw_exception(context, "var DOS_CLASSES not exist or not object");
					return;
				}

				JSVM::jsvm_value* Class = JSVM::jsvm_obj_get(context, dos_classes, "OBJECT_ID");
				if (!Class || !JSVM::jsvm_is_function(Class))
				{
					JSVM::jsvm_throw_exception(context, "var DOS_CLASSES not exist or not class");
					return;
				}
				serialize_values[0] = Class;


				Class = JSVM::jsvm_obj_get(context, dos_classes, "CSmartStruct");
				if (!Class || !JSVM::jsvm_is_function(Class))
				{
					JSVM::jsvm_throw_exception(context, "var CSmartStruct not exist or not class");
					return;
				}
				serialize_values[1] = Class;

				JSVM::jsvm_value* func = JSVM::jsvm_obj_get(context, module, PLUGIN_INIT_FN_NAME);
				if (!func || !JSVM::jsvm_is_function(func))
				{
					JSVM::jsvm_throw_exception_with_format(context, "function %s not exist or not function", PLUGIN_INIT_FN_NAME);
					return;
				}
				serialize_values[2] = func;

				func = JSVM::jsvm_obj_get(context, module, PLUGIN_CHECK_RELEASE_FN_NAME);
				if (!func || !JSVM::jsvm_is_function(func))
				{
					JSVM::jsvm_throw_exception_with_format(context, "function %s not exist or not function", PLUGIN_CHECK_RELEASE_FN_NAME);
					return;
				}
				serialize_values[3] = func;

				JSVM::jsvm_value_serialize(context, serialize_values, 4, JSClassSerializeData);
			}
		}
	}
}

double MyAdd(double a, double b, const char* Msg)
{
	printf("%s\n", Msg);
	return a + b;
}

#define CREATE_FUNC_TMPL(pScope, func, Name)\
	([pScope]()->jsvm_func_tmpl* {\
		jsvm_callback f1 = CallWrap<decltype(func), func>::CallbackFunc;\
		return jsvm_create_func_tmpl(pScope, f1, jsvm_func_standalone, func, Name);\
	})()

jsvm_vm* pVM = NULL;

class Worker :public CEasyThread
{
protected:
	jsvm_vm* m_pVM;
	virtual bool OnStart()
	{
		CEasyString ScriptRoot = CFileTools::GetModulePath(NULL);
		child_vm_setting Info;

		Info.parent_vm = pVM;
		Info.child_thread_id = 6;

		m_pVM = jsvm_create_vm(ScriptRoot, 0, NULL, &Info);
		jsvm_handle_scope* pScope = jsvm_create_handle_scope(m_pVM);
		jsvm_context* pContext = jsvm_get_context(pScope);

		jsvm_value* LoadFunc = jsvm_global_get(pContext, "require");
		if (LoadFunc && jsvm_is_function(LoadFunc))
		{
			CEasyString FileName = CFileTools::MakeModuleFullPath(NULL, "test.mjs");
			jsvm_value* result = JSCallGlobal<jsvm_value*>(pContext, "require", FileName);
			jsvm_process_exception(pScope);
		}

		jsvm_release_handle_scope(pScope);
		return true;

	}
	virtual bool OnRun()
	{
		jsvm_handle_scope* pScope = jsvm_create_handle_scope(m_pVM);
		jsvm_event_loop(pScope);
		jsvm_release_handle_scope(pScope);
		Sleep(1);
		return true;
	}
	virtual void OnTerminate()
	{
		jsvm_vm_dispose(m_pVM);
	}
};

int main()
{

	if (!JSVMApiManager::LoadApi("JSVMNodeD.dll"))
	{
		printf("JSVMNode.dll load failed\n");
		return 1;
	}
	CConsoleLogPrinter* pLog = new CConsoleLogPrinter();
	CLogManager::GetInstance()->AddChannel(LOG_JSVM_CHANNEL, pLog);
	pLog->Release();
	jsvm_platform_init("--expose-internals --experimental-require-module --inspect");

	CEasyString ScriptRoot = CFileTools::GetModulePath(NULL);

	Worker worker;

	pVM = jsvm_create_vm(ScriptRoot);

	worker.Start();

	jsvm_handle_scope* pScope = jsvm_create_handle_scope(pVM);
	jsvm_context* pContext = jsvm_get_context(pScope);


	//jsvm_func_tmpl* tmpl = CREATE_FUNC_TMPL(pScope, &MyAdd, "Add");

	//RegisterJSGlobalFunction<decltype(&MyAdd), &MyAdd>(pScope, "Add");

	//jsvm_callback f1 = CallWrap<decltype(&MyAdd), &MyAdd>::CallbackFunc;

	//jsvm_func_tmpl* tmpl = jsvm_create_func_tmpl(pScope, f1, jsvm_func_standalone, &MyAdd, "Add");


	//jsvm_value* pFunc = jsvm_create_func(pContext, tmpl);
	//jsvm_global_set(pContext, "Add", pFunc);

	//jsvm_func_tmpl* tmpl = jsvm_create_func_tmpl(pScope, Wait, jsvm_func_standalone, NULL, "Wait");
	//jsvm_value* pFunc = jsvm_create_func(pContext, tmpl);
	//jsvm_global_set(pContext, "Wait", pFunc);




	//CEasyArray<CEasyStringW> StrList;

	//StrList.Add("sdww"); 
	//StrList.Add("231");
	//StrList.Add("gnn");

	//jsvm_value* array = TypeConvertor<decltype(StrList)>::ToJS(pContext, StrList);
	//jsvm_global_set(pContext, "StrList", array);

	//CStringFile ScriptFile;

	//ScriptFile.SetLocalCodePage(CP_UTF8);

	//CEasyString FileName = CFileTools::MakeModuleFullPath(NULL, "out/JSTest.mjs");

	//if (ScriptFile.LoadFile(FileName, false))
	//{
	//	jsvm_value* LoadFunc = jsvm_global_get(pContext, "loadESMModule");
	//	if (LoadFunc && jsvm_is_function(LoadFunc))
	//	{
	//		jsvm_value* promise = JSCallGlobal<jsvm_value*>(pContext, "loadESMModule", (LPCTSTR)ScriptFile.GetData(), FileName);
	//		if (promise && jsvm_is_promise(promise))
	//		{
	//			//jsvm_tmpl* tmpl = jsvm_create_func_tmpl(pContext, OnModuleLoaded, JSVM::jsvm_func_type::jsvm_func_standalone, NULL, "OnModuleLoaded");
	//			JSVM::jsvm_tmpl* tmpl = JSVM::jsvm_create_func_tmpl(pContext,
	//				JSVM::CallWrapWithContext<decltype(&OnESMoudleLoaded), &OnESMoudleLoaded>::CallbackFunc,
	//				JSVM::jsvm_func_type::jsvm_func_standalone, NULL, "OnModuleLoaded");
	//			jsvm_value* func = jsvm_create_func(pContext, tmpl);
	//			jsvm_set_promise_then(pContext, promise, func);
	//		}
	//	}
	//}

	jsvm_value* LoadFunc = jsvm_global_get(pContext, "require");
	if (LoadFunc && jsvm_is_function(LoadFunc))
	{
		CEasyString FileName = CFileTools::MakeModuleFullPath(NULL, "out/JSTest.mjs");
		jsvm_value* result = JSCallGlobal<jsvm_value*>(pContext, "require", FileName);
		jsvm_process_exception(pScope);
		jsvm_value* Keys = jsvm_get_obj_keys(pContext, result, true);
		if (Keys)
		{
			UINT len = jsvm_get_array_length(pContext, Keys);
			for (UINT i = 0; i < len; i++)
			{
				jsvm_value* Key = jsvm_obj_get_by_index(pContext, Keys, i);
				if (Key)
					printf("%s\n", jsvm_get_value_string_utf8_no_buff(pContext, Key));
			}

		}
	}


	//jsvm_run_script(pScope, R"(
	//	import('file://E:/MyPrj/JSVMNode/Work/main.mjs').then((mod)=>{
	//		console.log(mod.const_str);
	//	});		
	//	console.log("test");		
	//)", 0, "load");
	//jsvm_run_script(pScope, R"(
	//	const { ESMLoader } = require('node:internal/modules/esm/loader');

	//	async function loadModule(specifier) {
	//	  // 创建 ESMLoader 实例
	//	  const loader = new ESMLoader();
 // 
	//	  // 解析和加载模块
	//	  const module = await loader.import(specifier);
 // 
	//	  return module;
	//	}
	//	loadModule('E:/MyPrj/JSVMNode/Work/main.mjs');
	//)", 0, "load");

	//array = jsvm_global_get(pContext, "StrList");
	//CEasyArray<CEasyStringW> ret = TypeConvertor<decltype(StrList)>::FromJS(pContext, array);

	//for (CEasyStringW& str : ret)
	//	printf("%s,", (LPCTSTR)UnicodeToAnsi(str));
	//printf("\n");

	//double c = JSCallGlobal<double>(pScope, "TestAdd", 3.4, 5.5);

	//worker.Start();

	while (true)
		jsvm_event_loop(pScope);

	//do {
	//	if (pResolver && WaitTimer.IsTimeOut())
	//	{
	//		jsvm_value* resolver = jsvm_get_value_from_persistent_value(pContext, pResolver);
	//		if (resolver)
	//			jsvm_resolver_resolve(pContext, resolver, NULL);
	//		jsvm_release_persistent_value(pResolver);
	//		pResolver = NULL;
	//	}
	//} while (jsvm_event_loop(pScope) || pResolver);

	//jsvm_value* global = jsvm_global(pContext);
	//jsvm_value* Keys = jsvm_get_obj_keys(pContext, global, true);
	//if (Keys)
	//{
	//	UINT len = jsvm_get_array_length(pContext, Keys);
	//	for (UINT i = 0; i < len; i++)
	//	{
	//		jsvm_value* Key = jsvm_obj_get_by_index(pContext, Keys, i);
	//		if (Key)
	//			printf("%s\n", jsvm_get_value_string_utf8_no_buff(pContext, Key));
	//	}

	//}

	jsvm_release_handle_scope(pScope);


	jsvm_vm_dispose(pVM);




	worker.SafeTerminate();

	jsvm_platform_dispose();
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
