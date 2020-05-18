// Copyright (c) 2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/sqlite.h>

#include <util/strencodings.h>
#include <util/translation.h>
#include <wallet/db.h>

#include <sqlite3.h>
#include <stdint.h>
#include <sqlite3.h>
#include <unordered_set>

namespace {
    RecursiveMutex cs_sqlite;
    //! Set of wallet file paths in use
    std::unordered_set<std::string> g_file_paths GUARDED_BY(cs_sqlite);
} // namespace

bool IsSQLiteWalletLoaded(const fs::path& wallet_path)
{
    fs::path data_path = WalletDataFilePath(wallet_path);
    LOCK(cs_sqlite);
    return g_file_paths.count(data_path.string()) > 0;
}

bool SQLiteDatabase::Verify(bilingual_str& error)
{
    return false;
}

static void ErrorLogCallback(void* arg, int code, const char* msg)
{
    assert(arg == nullptr); // That's what we tell it to do during the setup
    LogPrintf("SQLite Error. Code: %d. Message: %s\n", code, msg);
}

SQLiteDatabase::SQLiteDatabase(const fs::path& dir_path, const fs::path& file_path, bool mock) :
    WalletDatabase(), m_mock(mock), m_db(nullptr), m_file_path(file_path.string()), m_dir_path(dir_path.string())
{
    LogPrintf("Using SQLite Version %s\n", SQLiteDatabaseVersion());
    LogPrintf("Using wallet %s\n", m_dir_path);

    LOCK(cs_sqlite);
    if (g_file_paths.empty()) {
        // Setup logging
        int ret = sqlite3_config(SQLITE_CONFIG_LOG, ErrorLogCallback, nullptr);
        if (ret != SQLITE_OK) {
            throw std::runtime_error(strprintf("SQLiteDatabase: Failed to setup error log: %s\n", sqlite3_errstr(ret)));
        }
    }
    int ret = sqlite3_initialize();
    if (ret != SQLITE_OK) {
        throw std::runtime_error(strprintf("SQLiteDatabase: Failed to initialize SQLite: %s\n", sqlite3_errstr(ret)));
    }
    assert(g_file_paths.count(m_file_path) == 0);
    g_file_paths.insert(m_file_path);
}

SQLiteDatabase::~SQLiteDatabase()
{
    Close();
    LOCK(cs_sqlite);
    g_file_paths.erase(m_file_path);
    if (g_file_paths.empty()) {
        sqlite3_shutdown();
    }
}

void SQLiteDatabase::Open(const char* mode)
{
    m_read_only = (!strchr(mode, '+') && !strchr(mode, 'w'));
    if (m_dummy) return;

    bool create = strchr(mode, 'c') != nullptr;
    int flags = SQLITE_OPEN_NOFOLLOW; // Disallow symlink files
    if (m_read_only) {
        flags = SQLITE_OPEN_READONLY;
    } else {
        flags = SQLITE_OPEN_READWRITE;
    }
    if (create) {
        flags |= SQLITE_OPEN_CREATE;
    }
    if (m_mock) {
        flags = SQLITE_OPEN_MEMORY; // In memory database for mock db
    }

    if (m_db == nullptr) {
        sqlite3* db = nullptr;
        int ret = sqlite3_open_v2(m_file_path.c_str(), &db, flags, nullptr);
        if (ret != SQLITE_OK) {
            throw std::runtime_error(strprintf("SQLiteDatabase: Failed to open database: %s\n", sqlite3_errstr(ret)));
        }
        // TODO: Maybe(?) Check the file wasn't copied and a duplicate opened

        if (create) {
            bool table_exists;
            // Check that the main table exists
            sqlite3_stmt* check_main_stmt;
            std::string check_main = "SELECT name FROM sqlite_master WHERE type='table' AND name='main'";
            ret = sqlite3_prepare_v2(db, check_main.c_str(), -1, &check_main_stmt, nullptr);
            if (ret != SQLITE_OK) {
                throw std::runtime_error(strprintf("SQLiteDatabase: Failed to prepare statement to check table existence: %s\n", sqlite3_errstr(ret)));
            }
            ret = sqlite3_step(check_main_stmt);
            if (sqlite3_finalize(check_main_stmt) != SQLITE_OK) {
                throw std::runtime_error(strprintf("SQLiteDatabase: Failed to finalize statement checking table existence: %s\n", sqlite3_errstr(ret)));
            }
            if (ret == SQLITE_DONE) {
                table_exists = false;
            } else if (ret == SQLITE_ROW) {
                table_exists = true;
            } else {
                throw std::runtime_error(strprintf("SQLiteDatabase: Failed to execute statement to check table existence: %s\n", sqlite3_errstr(ret)));
            }

            if (!table_exists) {
                // Make the table for our key-value pairs
                std::string create_stmt = "CREATE TABLE main(key BLOB PRIMARY KEY, value BLOB)";
                ret = sqlite3_exec(db, create_stmt.c_str(), nullptr, nullptr, nullptr);
                if (ret != SQLITE_OK) {
                    throw std::runtime_error(strprintf("SQLiteDatabase: Failed to create new database: %s\n", sqlite3_errstr(ret)));
                }
            }
        }

        m_db = db;
    }
}

bool SQLiteDatabase::Rewrite(const char* skip)
{
    return false;
}

bool SQLiteDatabase::PeriodicFlush()
{
    return false;
}

bool SQLiteDatabase::Backup(const std::string& dest) const
{
    return false;
}

void SQLiteDatabase::Close()
{
    int res = sqlite3_close(m_db);
    if (res != SQLITE_OK) {
        throw std::runtime_error(strprintf("SQLiteDatabase: Failed to close database: %s\n", sqlite3_errstr(res)));
    }
    m_db = nullptr;
}

void SQLiteDatabase::Flush()
{
}

void SQLiteDatabase::ReloadDbEnv()
{
}

void SQLiteDatabase::Release()
{
}

void SQLiteDatabase::Acquire()
{
}

bool SQLiteDatabase::DBRead(CDataStream& key, CDataStream& value) const
{
    return false;
}

bool SQLiteDatabase::DBWrite(CDataStream& key, CDataStream& value, bool overwrite) const
{
    return false;
}

bool SQLiteDatabase::DBErase(CDataStream& key) const
{
    return false;
}

bool SQLiteDatabase::DBExists(CDataStream& key) const
{
    return false;
}

bool SQLiteDatabase::CreateCursor()
{
    return false;
}

bool SQLiteDatabase::ReadAtCursor(CDataStream& key, CDataStream& value, bool& complete)
{
    return false;
}

void SQLiteDatabase::CloseCursor()
{
}

bool SQLiteDatabase::TxnBegin()
{
    return false;
}

bool SQLiteDatabase::TxnCommit()
{
    return false;
}

bool SQLiteDatabase::TxnAbort()
{
    return false;
}

std::string SQLiteDatabaseVersion()
{
    return std::string(sqlite3_libversion());
}
