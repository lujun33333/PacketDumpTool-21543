"""Compile actual pool code and extracted ownership helper with deterministic mocks.

No server is started and no database is contacted. Build artifacts stay in --out.
"""
import argparse
import json
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def function_body(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


MOCK = r'''
#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
namespace sql {
class Connection;
inline std::function<void()> beforeConnect;
inline std::function<void(Connection*)> beforeReset;
class ResultSet {
    std::vector<unsigned> values; size_t pos = 0;
public:
    explicit ResultSet(std::vector<unsigned> v): values(std::move(v)) {}
    bool next() { return pos < values.size() ? (++pos, true) : false; }
    unsigned getUInt(const char*) { return values.at(pos - 1); }
};
class PreparedStatement {
    Connection* connection; std::map<unsigned, unsigned> params;
public:
    explicit PreparedStatement(Connection* c):connection(c) {}
    void setUInt(unsigned index, unsigned value) { params[index] = value; }
    ResultSet* executeQuery();
};
class Connection {
public:
    std::atomic<int> users{0};
    std::map<unsigned, std::map<unsigned, int>> inventory;
    int queries = 0;
    PreparedStatement* prepareStatement(const std::string& query) {
        if (query.find("AvailPeriod<=0") == std::string::npos ||
            query.find("ItemID IN (") == std::string::npos)
            throw std::runtime_error("unexpected ownership predicate");
        return new PreparedStatement(this);
    }
    void setAutoCommit(bool) { if (beforeReset) beforeReset(this); }
    void commit() {}
    void close() {}
    bool isValid() { return true; }
};
inline ResultSet* PreparedStatement::executeQuery() {
    ++connection->queries;
    const auto& rows = connection->inventory[params.at(1)];
    std::vector<unsigned> result;
    for (const auto& p : params) {
        if (p.first == 1) continue;
        auto row = rows.find(p.second);
        if (row != rows.end() && row->second <= 0) result.push_back(p.second);
    }
    return new ResultSet(result);
}
namespace mysql {
class Driver {
public:
    Connection* connect(const std::string&, const std::string&, const std::string&) {
        if (beforeConnect) beforeConnect();
        return new Connection;
    }
};
inline Driver* get_mysql_driver_instance() { static Driver driver; return &driver; }
}
}
'''

HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <future>
#include <iostream>
#include <thread>
#include "ConnectionPool.h"
using UINT = unsigned;
struct ItemInfo { UINT ItemID; };
#include "ownership.inc"
using namespace std::chrono_literals;

using BYTE = unsigned char;
using DWORD = unsigned long;
struct ClientNode { int ConnID = 1; int id = 0; };
std::recursive_mutex ClientMutex;
std::map<long, std::shared_ptr<ClientNode>> Clients;
constexpr int FE_PLAYER = 1, Notify = 1;
std::promise<void> broadcastEntered, broadcastRelease;
auto broadcastGate = broadcastRelease.get_future().share();
std::vector<int> recipientsSeen;
void SendToClientOfficial14(const std::shared_ptr<ClientNode>& c, int,
    const BYTE*, DWORD, short, int, short, int) {
    recipientsSeen.push_back(c->id);
    if (recipientsSeen.size() == 1) {
        broadcastEntered.set_value(); broadcastGate.wait();
    }
}
#include "broadcast.inc"

void broadcastSnapshot() {
    for (int i = 0; i < 130; ++i) {
        auto c = std::make_shared<ClientNode>(); c->id = i; Clients[i] = c;
    }
    std::weak_ptr<ClientNode> removed = Clients[129];
    std::thread sender(broadcastProbe);
    broadcastEntered.get_future().wait();
    auto registry = std::async(std::launch::async, [] {
        std::lock_guard<std::recursive_mutex> lock(ClientMutex);
        Clients.erase(129);
    });
    const bool registryUsable = registry.wait_for(2s) == std::future_status::ready;
    const bool retained = !removed.expired();
    broadcastRelease.set_value(); sender.join(); registry.get();
    assert(registryUsable && retained && removed.expired());
    assert(recipientsSeen.size() == 130);
    for (int i = 0; i < 130; ++i) assert(recipientsSeen[i] == i);
    std::cout << "PASS 130-recipient snapshot: slow send does not block registry; lifetime and iteration order retained\n";
}

void concurrentExpansion() {
    ConnectionPool pool;
    pool.initialize(1, "mock", "mock", "", "mock");
    auto held = pool.getConnection();
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::atomic<int> creates{0}, completed{0}, conflicts{0};
    sql::beforeConnect = [&] { ++creates; entered.set_value(); gate.wait(); };
    std::thread creator([&] { auto c = pool.getConnection(); pool.returnConnection(c); });
    entered.get_future().wait();
    std::vector<std::thread> waiters;
    for (int i = 0; i < 129; ++i) waiters.emplace_back([&] {
        for (int j = 0; j < 8; ++j) {
            auto c = pool.getConnection();
            if (c->users.fetch_add(1) != 0) ++conflicts;
            std::this_thread::yield();
            c->users.fetch_sub(1);
            pool.returnConnection(c);
        }
        ++completed;
    });
    pool.returnConnection(held);
    const auto deadline = std::chrono::steady_clock::now() + 4s;
    while (completed != 129 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    const bool progressedDuringConnect = completed == 129;
    release.set_value();
    creator.join();
    for (auto& t : waiters) t.join();
    sql::beforeConnect = {};
    assert(progressedDuringConnect && conflicts == 0 && creates == 1);
    assert(pool.GetConnectionsTimes() == 0 && pool.GetAvailableConnections() == 2);
    std::cout << "PASS 130 callers: returned connection usable during stalled connect; no duplicate lease\n";
}

void slowReset() {
    ConnectionPool pool;
    pool.initialize(2, "mock", "mock", "", "mock");
    auto a = pool.getConnection(), b = pool.getConnection();
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    sql::beforeReset = [&](sql::Connection* c) {
        if (c == a.get()) { entered.set_value(); gate.wait(); }
    };
    std::thread reset([&] { pool.returnConnection(a); });
    entered.get_future().wait();
    auto other = std::async(std::launch::async, [&] {
        pool.returnConnection(b);
        auto c = pool.getConnection();
        const bool same = c == b;
        pool.returnConnection(c);
        return same;
    });
    const bool progressed = other.wait_for(2s) == std::future_status::ready;
    release.set_value(); reset.join();
    assert(other.get() && progressed);
    sql::beforeReset = {};
    pool.returnConnection(a); // duplicate returns must not enqueue twice
    assert(pool.GetAvailableConnections() == 2);
    std::cout << "PASS slow reset does not block unrelated connection; duplicate return rejected\n";
}

void failedExpansion() {
    ConnectionPool pool;
    pool.initialize(0, "mock", "mock", "", "mock");
    sql::beforeConnect = [] { throw std::runtime_error("injected connect failure"); };
    bool failed = false;
    try { pool.getConnection(); } catch (...) { failed = true; }
    sql::beforeConnect = {};
    auto next = std::async(std::launch::async, [&] {
        auto c = pool.getConnection(); pool.returnConnection(c);
    });
    assert(next.wait_for(2s) == std::future_status::ready);
    next.get(); assert(failed && pool.GetAvailableConnections() == 1);
    std::cout << "PASS expansion failure releases creation state\n";
}

void ownedCandidates() {
    sql::Connection db;
    std::vector<ItemInfo> candidates;
    for (unsigned id = 10000; id < 11000; ++id) {
        candidates.push_back({id});
        if (id % 4 != 3) db.inventory[10001][id] = id % 4 == 0 ? -1 : (id % 4 == 1 ? 0 : 24);
    }
    db.inventory[10002][10003] = -1; // must not mix player ownership
    db.inventory[10001][99999] = -1; // must not include non-candidates
    for (int pass = 0; pass < 2; ++pass) {
        const auto result = Treasure_LoadOwnedCandidates(&db, 10001, candidates);
        for (auto c : candidates) {
            const auto found = db.inventory[10001].find(c.ItemID);
            const bool oldPredicate = found != db.inventory[10001].end() && found->second <= 0;
            assert((result.count(c.ItemID) != 0) == oldPredicate);
        }
        assert(result.count(99999) == 0);
        assert(db.queries == 4 * (pass + 1));
        db.inventory[10001][10003] = -1; // next visit must see own transaction write
    }
    const int queries = db.queries;
    assert(Treasure_LoadOwnedCandidates(&db, 10001, {}).empty());
    assert(db.queries == queries);
    assert(Treasure_LoadOwnedCandidates(&db, 10001, {{10000},{10000}}).size() == 1);
    std::cout << "PASS ownership predicate, player isolation, timed/permanent, chunk boundaries, reload, duplicates, empty pool\n";
}

int main() { concurrentExpansion(); slowReset(); failedExpansion(); ownedCandidates(); broadcastSnapshot(); }
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--baseline", type=pathlib.Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "Message.cpp").read_text(encoding="utf-8")
    original = (args.baseline / "Message.cpp").read_text(encoding="utf-8")
    # Normalize only the ownership lookup change; all RNG, reward, limit,
    # lucky-value, write/commit and notification statements must stay identical.
    for name in ("GetRandItem2", "GetRandItemCJ"):
        signature = "bool " + name + "(std::shared_ptr<ClientNode> Client, UINT Itemid, short count, std::vector<ItemInfo>& ItemsAdd, std::vector<ItemInfo>& items)\n"
        old = function_body(original, signature)
        new = function_body(source, signature)
        assert new.count("Treasure_LoadOwnedCandidates(") == 1
        old = re.sub(r'\s*std::unique_ptr<sql::PreparedStatement> prep_stmt\(conn->prepareStatement\("SELECT 1 FROM Item Where ItemID=\? and Uin=\? And AvailPeriod <= 0;"\)\);.*?if \(res->next\(\)\)', ' if (OWNED)', old, count=1, flags=re.S)
        old = old.replace("res.reset();;", "")
        new = re.sub(r'const auto ownedCandidates = Treasure_LoadOwnedCandidates\([^;]+;', '', new)
        new = new.replace("if (ownedCandidates.count(TreasureAwardRand.AwardItems[i].ItemID))", "if (OWNED)")
        assert re.sub(r'\s+', '', old) == re.sub(r'\s+', '', new), name + " changed lottery semantics"
    room = (ROOT / "Room.cpp").read_text(encoding="utf-8")
    broadcast = function_body(room, "static void NotifyBugletTalkInternal(")
    snapshot = broadcast.index("std::vector<std::shared_ptr<ClientNode>> recipients;")
    sendloop = broadcast.index("for (const auto& DstClient : recipients)", snapshot)
    depth = 0
    for char in broadcast[snapshot:sendloop]:
        depth += (char == "{") - (char == "}")
    assert depth == 0 and "ClientMutex" in broadcast[snapshot:sendloop]
    assert "ClientMutex" not in broadcast[sendloop:]
    old_broadcast = function_body((args.baseline / "Room.cpp").read_text(encoding="utf-8"), "static void NotifyBugletTalkInternal(")
    old_snapshot = old_broadcast.index("std::lock_guard<std::recursive_mutex> lg(ClientMutex);")
    assert broadcast[:snapshot].rstrip() == old_broadcast[:old_snapshot].rstrip(), "broadcast packet format changed"
    print("PASS source audit: RNG/awards/limits/commit unchanged; broadcast sends outside registry lock", flush=True)
    for relative in ("jdbc/mysql_connection.h", "jdbc/mysql_driver.h", "jdbc/cppconn/statement.h", "jdbc/cppconn/prepared_statement.h"):
        target = out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#include "mock.h"\n', encoding="utf-8")
    (out / "spdlog").mkdir(exist_ok=True)
    (out / "spdlog/spdlog.h").write_text('#include <memory>\nnamespace spdlog { struct logger { template<class... T> void info(T&&...) {} void flush() {} }; inline std::shared_ptr<logger> get(const char*) { return {}; } }\n', encoding="utf-8")
    (out / "mock.h").write_text(MOCK, encoding="utf-8")
    (out / "ownership.inc").write_text(function_body(source, "static std::unordered_set<UINT> Treasure_LoadOwnedCandidates("), encoding="utf-8")
    (out / "broadcast.inc").write_text('void broadcastProbe() { auto Client = std::make_shared<ClientNode>(); std::vector<BYTE> buf(1); size_t len = 1;\n' + broadcast[snapshot:], encoding="utf-8")
    (out / "regression.cpp").write_text(HARNESS, encoding="utf-8")
    vcvars = pathlib.Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat")
    command = f'call "{vcvars}" >nul && cl /nologo /std:c++17 /EHsc /utf-8 /I"{out}" /I"{ROOT}" "{out / "regression.cpp"}" "{ROOT / "ConnectionPool.cpp"}" /Fe:"{out / "regression.exe"}"'
    (out / "compile.cmd").write_text("@echo off\n" + command + "\n", encoding="utf-8")
    subprocess.run(["cmd.exe", "/d", "/c", "compile.cmd"], cwd=out, check=True, timeout=90)
    subprocess.run([str(out / "regression.exe")], cwd=out, check=True, timeout=30)
    print(json.dumps({"result": "PASS", "scope": "mock regression; not live server load test"}))


if __name__ == "__main__":
    main()
