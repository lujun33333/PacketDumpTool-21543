"""Compile actual old/new inventory preparation functions; no DB or server writes."""
import argparse
import pathlib
import subprocess
from LotteryConcurrencyRegression import function_body

ROOT = pathlib.Path(__file__).resolve().parents[1]
SIGNATURE = "static std::vector<ItemInfo> QQS_PrepareOfficialInventoryItems("


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=pathlib.Path, required=True)
    args = parser.parse_args()
    out = args.baseline.resolve() / "regression"
    out.mkdir(exist_ok=True)
    current = (ROOT / "Message.cpp").read_text(encoding="utf-8")
    baseline = (args.baseline / "Message.cpp").read_text(encoding="utf-8")
    old_fn = function_body(baseline, SIGNATURE)
    new_fn = function_body(current, SIGNATURE)
    removed_start = old_fn.index("\t\tif (original.ItemID >=")
    removed_end = old_fn.index("\t\tItemInfo item = original;", removed_start)
    assert old_fn[:removed_start] + old_fn[removed_end:] == new_fn
    assert "kQQS21622InvalidCatalogIndex" not in current
    constants = "\n".join(line for line in baseline.splitlines() if line.startswith("static constexpr UINT kQQS21622InvalidCatalogIndex"))
    program = r'''
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
using UINT = unsigned;
constexpr UINT EAIT_CAR = 1, EAIT_SKIN = 2;
struct ClientNode { UINT Uin = 10001; UINT KartID = 20000; };
struct ItemInfo { UINT ItemID; UINT ItemNum = 7; int AvailPeriod = -1; UINT Status = 1; UINT ItemType = 2; };
UINT GetItemType(UINT id) { return id == 20000 ? EAIT_CAR : (id == 20001 ? EAIT_SKIN : 0); }
void QQS_DebugPrintf10001(const char*, ...) {}
'''
    program += constants + "\n" + old_fn.replace("QQS_PrepareOfficialInventoryItems", "OldPrepare") + "\n" + new_fn
    program += r'''
int main() {
    auto client = std::make_shared<ClientNode>();
    std::vector<ItemInfo> input = {{175144}, {175145}, {176330}, {177868}, {177869}, {20000}, {20001}};
    const auto old = OldPrepare(client, input);
    const auto now = QQS_PrepareOfficialInventoryItems(client, input);
    assert(old.size() == 4 && now.size() == input.size());
    for (size_t i = 0; i < now.size(); ++i) {
        assert(now[i].ItemID == input[i].ItemID);
        assert(now[i].ItemNum == input[i].ItemNum && now[i].AvailPeriod == input[i].AvailPeriod);
        assert(now[i].ItemType == 0);
        assert(now[i].Status == (input[i].ItemID == 20001 ? 0u : 1u));
    }
    for (auto& previous : old) {
        bool found = false;
        for (auto& item : now) if (item.ItemID == previous.ItemID) {
            assert(item.ItemNum == previous.ItemNum && item.AvailPeriod == previous.AvailPeriod);
            assert(item.Status == previous.Status && item.ItemType == previous.ItemType); found = true;
        }
        assert(found);
    }
    assert(QQS_PrepareOfficialInventoryItems(client, {}).empty());
    assert(QQS_PrepareOfficialInventoryItems(nullptr, input).size() == input.size());
    std::cout << "PASS 176330 + both range boundaries retained; prior items/counts/periods/status unchanged; empty/null cases pass\n";
}
'''
    (out / "inventory.cpp").write_text(program, encoding="utf-8")
    vcvars = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    (out / "compile.cmd").write_text(f'@echo off\ncall "{vcvars}" >nul && cl /nologo /std:c++17 /EHsc /utf-8 inventory.cpp /Fe:inventory.exe\n', encoding="utf-8")
    subprocess.run(["cmd.exe", "/d", "/c", "compile.cmd"], cwd=out, check=True, timeout=60)
    subprocess.run([str(out / "inventory.exe")], cwd=out, check=True, timeout=10)


if __name__ == "__main__":
    main()
