import { MyAdder } from "./class";

export class MainClass {
    public Print(Msg: string) {
        console.log(Msg);
    }
    public DoAdd(a: number, b: number): number {
        let Adder = new MyAdder();
        let c = Adder.Add(a, b);
        return c;
    }
}

let main = new MainClass();
let ret = main.DoAdd(12, 34);
main.Print(ret.toString());