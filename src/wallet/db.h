// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_DB_H
#define BITCOIN_WALLET_DB_H

#include <clientversion.h>
#include <fs.h>
#include <serialize.h>
#include <streams.h>
#include <util/system.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsuggest-override"
#endif
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

struct bilingual_str;

enum class StorageType : int
{
    NONE,
    BDB,
    SQLITE,
};

/** Given a wallet directory path or legacy file path, return path to main data file in the wallet database. */
void SplitWalletPath(const fs::path& wallet_path, fs::path& env_directory, std::string& database_filename);
fs::path WalletDataFilePath(const fs::path& wallet_path);

/** An instance of this class represents one database.
 **/
class WalletDatabase
{
private:
    virtual bool DBRead(CDataStream& key, CDataStream& value) const = 0;
    virtual bool DBWrite(CDataStream& key, CDataStream& value, bool overwrite=true) const = 0;
    virtual bool DBErase(CDataStream& key) const = 0;
    virtual bool DBExists(CDataStream& key) const = 0;

public:
    /** Create dummy DB handle */
    WalletDatabase() : nUpdateCounter(0), nLastSeen(0), nLastFlushed(0), nLastWalletUpdate(0) {}
    virtual ~WalletDatabase() {}

    std::atomic<unsigned int> nUpdateCounter;
    unsigned int nLastSeen;
    unsigned int nLastFlushed;
    int64_t nLastWalletUpdate;

    /** Open the database if it is not already opened. */
    virtual void Open(const char* mode) = 0;

    /** Fetch the database filename */
    virtual std::string GetFilePath() const = 0;

    //! Counts the number of active database users to be sure that the database is not closed while someone is using it
    std::atomic<int> m_refcount{0};
    /** Indicate the a new database user has began using the database. Increments m_refcount */
    virtual void Acquire() = 0;
    /** Indicate that database user has stopped using the database. Decrement m_refcount */
    virtual void Release() = 0;

    /** Rewrite the entire database on disk, with the exception of key pszSkip if non-zero
     */
    virtual bool Rewrite(const char* pszSkip=nullptr) = 0;

    /** Back up the entire database to a file.
     */
    virtual bool Backup(const std::string& strDest) const = 0;

    /** Close the database and make sure all changes are flushed to disk.
     */
    virtual void Close() = 0;
    /** Just flush the changes to disk, but not necessarily clean up environment stuff like log files */
    virtual void Flush() = 0 ;
    /* flush the wallet passively (TRY_LOCK)
       ideal to be called periodically */
    virtual bool PeriodicFlush() = 0;

    void IncrementUpdateCounter();

    virtual void ReloadDbEnv() = 0;

    /* verifies the environment and database file */
    virtual bool Verify(bilingual_str& errorStr) = 0;

    template <typename K, typename T>
    bool Read(const K& key, T& value)
    {
        // Key
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;

        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        bool success = false;
        bool ret = DBRead(ssKey, ssValue);
        if (ret) {
            // Unserialize value
            try {
                ssValue >> value;
                success = true;
            } catch (const std::exception&) {
                // In this case success remains 'false'
            }
        }
        return ret && success;
    }

    template <typename K, typename T>
    bool Write(const K& key, const T& value, bool fOverwrite = true)
    {
        // Key
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;

        // Value
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        ssValue.reserve(10000);
        ssValue << value;

        // Write
        return DBWrite(ssKey, ssValue, fOverwrite);
    }

    template <typename K>
    bool Erase(const K& key)
    {
        // Key
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;

        // Erase
        return DBErase(ssKey);
    }

    template <typename K>
    bool Exists(const K& key)
    {
        // Key
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;

        // Exists
        return DBExists(ssKey);
    }

    virtual bool CreateCursor() = 0;
    virtual bool ReadAtCursor(CDataStream& ssKey, CDataStream& ssValue, bool& complete) = 0;
    virtual void CloseCursor() = 0;
    virtual bool TxnBegin() = 0;
    virtual bool TxnCommit() = 0;
    virtual bool TxnAbort() = 0;

    virtual StorageType GetStorageType() const = 0;
};

#endif // BITCOIN_WALLET_DB_H
