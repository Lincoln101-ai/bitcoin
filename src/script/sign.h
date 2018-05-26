// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_SIGN_H
#define BITCOIN_SCRIPT_SIGN_H

#include <hash.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <streams.h>

class CKey;
class CKeyID;
class CScript;
class CScriptID;
class CTransaction;

struct CMutableTransaction;

/** An interface to be implemented by keystores that support signing. */
class SigningProvider
{
public:
    virtual ~SigningProvider() {}
    virtual bool GetCScript(const CScriptID &scriptid, CScript& script) const =0;
    virtual bool GetPubKey(const CKeyID &address, CPubKey& pubkey) const =0;
    virtual bool GetKey(const CKeyID &address, CKey& key) const =0;
};

/** Interface for signature creators. */
class BaseSignatureCreator {
public:
    virtual ~BaseSignatureCreator() {}
    virtual const BaseSignatureChecker& Checker() const =0;

    /** Create a singular (non-script) signature. */
    virtual bool CreateSig(const SigningProvider& provider, std::vector<unsigned char>& vchSig, const CKeyID& keyid, const CScript& scriptCode, SigVersion sigversion) const =0;
};

/** A signature creator for transactions. */
class TransactionSignatureCreator : public BaseSignatureCreator {
    const CTransaction* txTo;
    unsigned int nIn;
    int nHashType;
    CAmount amount;
    const TransactionSignatureChecker checker;

public:
    TransactionSignatureCreator(const CTransaction* txToIn, unsigned int nInIn, const CAmount& amountIn, int nHashTypeIn=SIGHASH_ALL);
    const BaseSignatureChecker& Checker() const override { return checker; }
    bool CreateSig(const SigningProvider& provider, std::vector<unsigned char>& vchSig, const CKeyID& keyid, const CScript& scriptCode, SigVersion sigversion) const override;
};

class MutableTransactionSignatureCreator : public TransactionSignatureCreator {
    CTransaction tx;

public:
    MutableTransactionSignatureCreator(const CMutableTransaction* txToIn, unsigned int nInIn, const CAmount& amountIn, int nHashTypeIn) : TransactionSignatureCreator(&tx, nInIn, amountIn, nHashTypeIn), tx(*txToIn) {}
};

/** A signature creator that just produces 72-byte empty signatures. */
extern const BaseSignatureCreator& DUMMY_SIGNATURE_CREATOR;

struct SignatureData {
    CScript scriptSig;
    CScriptWitness scriptWitness;

    SignatureData() {}
    explicit SignatureData(const CScript& script) : scriptSig(script) {}
};



// Note: These constants are in reverse byte order because serialization uses LSB
static constexpr uint32_t PSBT_MAGIC_BYTES = 0x74627370;
static constexpr uint8_t PSBT_UNSIGNED_TX_NON_WITNESS_UTXO = 0x00;
static constexpr uint8_t PSBT_REDEEMSCRIPT_WITNESS_UTXO = 0x01;
static constexpr uint8_t PSBT_WITNESSSCRIPT_PARTIAL_SIG = 0x02;
static constexpr uint8_t PSBT_BIP32_KEYPATH_SIGHASH = 0x03;
static constexpr uint8_t PSBT_NUM_IN_VIN = 0x04;

// The separator is 0x00. Reading this in means that the unserializer can interpret it
// as a 0 length key. which indicates that this is the separator. The separator has no value.
static const uint8_t PSBT_SEPARATOR = 0x00;

template<typename... X>
std::vector<unsigned char> SerializeToVector(const X&... args)
{
    std::vector<unsigned char> ret;
    CVectorWriter ss(SER_NETWORK, PROTOCOL_VERSION, ret, 0);
    SerializeMany(ss, args...);
    return ret;
}

/** An structure for PSBTs which contain per input information */
struct PartiallySignedInput
{
    CTransactionRef non_witness_utxo;
    CTxOut witness_utxo;
    std::map<CPubKey, std::vector<unsigned char>> partial_sigs;
    std::map<std::vector<unsigned char>, std::vector<unsigned char>> unknown;
    int sighash_type = 0;
    uint64_t index = 0;
    bool use_in_index = false;

    PartiallySignedInput() {}
    void SetNull();
    bool IsNull();
};

/** A version of CTransaction with the PSBT format*/
struct PartiallySignedTransaction
{
    CMutableTransaction tx;
    std::map<uint160, CScript> redeem_scripts;
    std::map<uint256, CScript> witness_scripts;
    std::vector<PartiallySignedInput> inputs;
    std::map<std::vector<unsigned char>, std::vector<unsigned char>> unknown;
    std::map<CPubKey, std::vector<uint32_t>> hd_keypaths;
    uint64_t num_ins = 0;
    bool use_in_index = false;

    PartiallySignedTransaction() {}
    PartiallySignedTransaction(const CMutableTransaction& tx, const std::map<uint160, CScript>& redeem_scripts, const std::map<uint256, CScript>& witness_scripts, const std::vector<PartiallySignedInput>& inputs);

    template <typename Stream>
    inline void Serialize(Stream& s) const {

        // magic bytes
        s << PSBT_MAGIC_BYTES << (char)0xff; // psbt 0xff

        // Write transaction if it exists
        if (!CTransaction(tx).IsNull()) {
            // unsigned tx flag
            s << SerializeToVector(PSBT_UNSIGNED_TX_NON_WITNESS_UTXO);

            // Write serialized tx to a stream
            s << SerializeToVector(tx);
        }

        // Write redeem scripts and witness scripts
        for (auto& entry : redeem_scripts) {
            s << SerializeToVector(PSBT_REDEEMSCRIPT_WITNESS_UTXO, entry.first);
            s << entry.second;
        }
        for (auto& entry : witness_scripts) {
            s << SerializeToVector(PSBT_WITNESSSCRIPT_PARTIAL_SIG, entry.first);
            s << entry.second;
        }
        // Write any hd keypaths
        for (auto keypath_pair : hd_keypaths) {
            s << SerializeToVector(PSBT_BIP32_KEYPATH_SIGHASH, MakeSpan(keypath_pair.first));
            WriteCompactSize(s, keypath_pair.second.size() * sizeof(uint32_t));
            for (auto& path : keypath_pair.second) {
                s << path;
            }
        }

        // Write the number of inputs
        if (num_ins > 0) {
            s << SerializeToVector(PSBT_NUM_IN_VIN);
            s << SerializeToVector(COMPACTSIZE(num_ins));
        }

        // Write the unknown things
        for(auto& entry : unknown) {
            s << entry.first;
            s << entry.second;
        }

        // Separator
        s << PSBT_SEPARATOR;

        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            CTxIn in = tx.vin[i];
            PartiallySignedInput psbt_in = inputs[i];
            if (in.scriptSig.empty() && in.scriptWitness.IsNull()) {
                // Write the utxo
                // If there is a non-witness utxo, then don't add the witness one.
                if (psbt_in.non_witness_utxo) {
                    s << SerializeToVector(PSBT_UNSIGNED_TX_NON_WITNESS_UTXO);
                    s << SerializeToVector(psbt_in.non_witness_utxo);
                } else if (!psbt_in.witness_utxo.IsNull()) {
                    s << SerializeToVector(PSBT_REDEEMSCRIPT_WITNESS_UTXO);
                    s << SerializeToVector(psbt_in.witness_utxo);
                }

                // Write any partial signatures
                for (auto sig_pair : psbt_in.partial_sigs) {
                    s << SerializeToVector(PSBT_WITNESSSCRIPT_PARTIAL_SIG, MakeSpan(sig_pair.first));
                    s << sig_pair.second;
                }

                // Write the sighash type
                if (psbt_in.sighash_type > 0) {
                    s << SerializeToVector(PSBT_BIP32_KEYPATH_SIGHASH);
                    s << SerializeToVector(psbt_in.sighash_type);
                }

                // Write the index
                if (use_in_index) {
                    s << SerializeToVector(PSBT_NUM_IN_VIN);
                    s << SerializeToVector(COMPACTSIZE(psbt_in.index));
                }
            }

            // Write unknown things
            for(auto& entry : psbt_in.unknown) {
                s << entry.first;
                s << entry.second;
            }

            s << PSBT_SEPARATOR;
        }
    }


    template <typename Stream>
    inline void Unserialize(Stream& s) {
        // Read the magic bytes
        int magic;
        s >> magic;
        if (magic != PSBT_MAGIC_BYTES) {
            throw std::ios_base::failure("Invalid PSBT magic bytes");
        }
        unsigned char magic_sep;
        s >> magic_sep;

        // hashers
        CHash160 hash160_hasher;
        CSHA256 sha256_hasher;

        // Read loop
        uint32_t separators = 0;
        PartiallySignedInput input;
        input.index = 0; // Make the first input at index 0
        bool in_globals = true;
        while(!s.empty()) {
            // Reset hashers
            hash160_hasher.Reset();
            sha256_hasher.Reset();

            // read size of key
            uint64_t key_len = ReadCompactSize(s);

            // the key length is 0 if that was actually a separator byte
            // This is a special case for key lengths 0 as those are not allowed (except for separator)
            if (key_len == 0) {

                // If we ever hit a separator, we are no longer in globals
                in_globals = false;

                if (separators > 0) {
                    // Make sure this input has an index of indexes are being used
                    if (!use_in_index && !input.use_in_index) {
                        throw std::ios_base::failure("Input indexes being used but an input was provided without an index");
                    }

                    // Add input to vector
                    inputs.push_back(input);
                    input.SetNull();
                    input.index = separators - 1; // Set the inputs index. This will be overwritten if an index is provided.
                }

                ++separators;

                // Make sure that the number of separators - 1 matches the number of inputs if the stream is going to be empty
                if (s.empty() && separators - 1 == num_ins) {
                    throw std::ios_base::failure("Inputs provided does not match the number of inputs stated.");
                }

                continue;
            }

            // Read key
            std::vector<unsigned char> key;
            key.resize(key_len);
            Span<unsigned char> key_data = MakeSpan(key);
            s >> key_data;

            // First byte of key is the type
            unsigned char type = key[0];

            // Read in value length
            uint64_t value_len = ReadCompactSize(s);

            // Do stuff based on type
            switch(type) {
                // Raw transaction or a non witness utxo
                case PSBT_UNSIGNED_TX_NON_WITNESS_UTXO:
                    if (in_globals) {
                        s >> tx;
                    } else {
                        // Read in the transaction
                        CTransactionRef prev_tx;
                        s >> prev_tx;

                        // Check that this utxo matches this input
                        if (tx.vin[input.index].prevout.hash.Compare(prev_tx->GetHash()) != 0) {
                            throw std::ios_base::failure("Provided non witness utxo does not match the required utxo for input");
                        }

                        // Add to input
                        input.non_witness_utxo = std::move(prev_tx);
                    }
                    break;
                // redeemscript or a witness utxo
                case PSBT_REDEEMSCRIPT_WITNESS_UTXO:
                    if (in_globals) {
                        // Make sure that the key is the size of uint160 + 1
                        if (key.size() != 21) {
                            throw std::ios_base::failure("Size of key was not the expected size for the type redeem script");
                        }
                        // Retrieve hash160 from key
                        uint160 hash160(std::vector<unsigned char>(key.begin() + 1, key.end()));

                        // Read in the redeemscript
                        std::vector<unsigned char> redeemscript_bytes;
                        redeemscript_bytes.resize(value_len);
                        Span<unsigned char> redeemscript_data = MakeSpan(redeemscript_bytes);
                        s >> redeemscript_data;
                        CScript redeemscript(redeemscript_bytes.begin(), redeemscript_bytes.end());

                        // Check that the redeemscript hash160 matches the one provided
                        hash160_hasher.Write(&redeemscript_bytes[0], redeemscript_bytes.size());
                        unsigned char rs_hash160_data[hash160_hasher.OUTPUT_SIZE];
                        hash160_hasher.Finalize(rs_hash160_data);
                        uint160 rs_hash160(std::vector<unsigned char>(rs_hash160_data, rs_hash160_data + hash160_hasher.OUTPUT_SIZE));
                        if (hash160.Compare(rs_hash160) != 0) {
                            throw std::ios_base::failure("Provided hash160 does not match the redeemscript's hash160");
                        }

                        // Add to map
                        redeem_scripts.emplace(hash160, redeemscript);
                    } else {
                        // Read in the utxo
                        CTxOut vout;
                        s >> vout;

                        // Add to map
                        input.witness_utxo = vout;
                    }
                    break;
                // witnessscript or a partial signature
                case PSBT_WITNESSSCRIPT_PARTIAL_SIG:
                    if (in_globals) {
                        // Make sure that the key is the size of uint256 + 1
                        if (key.size() != 33) {
                            throw std::ios_base::failure("Size of key was not the expected size for the type witness script");
                        }
                        // Retrieve sha256 from key
                        uint256 hash(std::vector<unsigned char>(key.begin() + 1, key.end()));

                        // Read in the redeemscript
                        std::vector<unsigned char> witnessscript_bytes;
                        witnessscript_bytes.resize(value_len);
                        Span<unsigned char> witnessscript_data = MakeSpan(witnessscript_bytes);
                        s >> witnessscript_data;
                        CScript witnessscript(witnessscript_bytes.begin(), witnessscript_bytes.end());

                        // Check that the witnessscript sha256 matches the one provided
                        sha256_hasher.Write(&witnessscript_bytes[0], witnessscript_bytes.size());
                        unsigned char ws_sha256_data[sha256_hasher.OUTPUT_SIZE];
                        sha256_hasher.Finalize(ws_sha256_data);
                        uint256 ws_sha256(std::vector<unsigned char>(ws_sha256_data, ws_sha256_data + sha256_hasher.OUTPUT_SIZE));
                        if (hash.Compare(ws_sha256) != 0) {
                            throw std::ios_base::failure("Provided sha256 does not match the witnessscript's sha256");
                        }

                        // Add to map
                        witness_scripts.emplace(hash, witnessscript);
                    } else {
                        // Make sure that the key is the size of pubkey + 1
                        if (key.size() != CPubKey::PUBLIC_KEY_SIZE + 1 && key.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE + 1) {
                            throw std::ios_base::failure("Size of key was not the expected size for the type partial signature pubkey");
                        }
                        // Read in the pubkey from key
                        CPubKey pubkey(key.begin() + 1, key.end());

                        // Read in the signature from value
                        std::vector<unsigned char> sig;
                        sig.resize(value_len);
                        Span<unsigned char> sig_data = MakeSpan(sig);
                        s >> sig_data;

                        // Add to list
                        input.partial_sigs.emplace(pubkey, sig);
                    }
                    break;
                // BIP 32 HD Keypaths and sighash types
                case PSBT_BIP32_KEYPATH_SIGHASH:
                {
                    if (in_globals) {
                        // Make sure that the key is the size of pubkey + 1
                         if (key.size() != CPubKey::PUBLIC_KEY_SIZE + 1 && key.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE + 1) {
                            throw std::ios_base::failure("Size of key was not the expected size for the type BIP32 keypath");
                        }
                        // Read in the pubkey from key
                        CPubKey pubkey(key.begin() + 1, key.end());

                        // Read in key path
                        std::vector<uint32_t> keypath;
                        for (unsigned int i = 0; i < value_len; i += sizeof(uint32_t)) {
                            uint32_t index;
                            s >> index;
                            keypath.push_back(index);
                        }

                        // Add to map
                        hd_keypaths.emplace(pubkey, keypath);
                    } else {
                        // Read in the sighash type
                        s >> input.sighash_type;
                    }
                    break;
                }
                // Number of inputs and input index
                case PSBT_NUM_IN_VIN:
                {
                    if (in_globals) {
                        num_ins = ReadCompactSize(s);
                    } else {
                        // Make sure that we are using inout indexes or this is the first input
                        if (!use_in_index && separators != 1) {
                            throw std::ios_base::failure("Input indexes being used but an input does not provide its index");
                        }

                        s >> input.index;
                        use_in_index = true;
                        input.use_in_index = true;
                    }
                }
                // Unknown stuff
                default:
                    // Read in the value
                    std::vector<unsigned char> val_bytes;
                    val_bytes.resize(value_len);
                    Span<unsigned char> value = MakeSpan(val_bytes);
                    s >> value;

                    // global data
                    if (in_globals) {
                        unknown.emplace(std::move(key), std::move(val_bytes));
                    } else {
                        input.unknown.emplace(std::move(key), std::move(val_bytes));
                    }
            }
        }
    }

    template <typename Stream>
    PartiallySignedTransaction(deserialize_type, Stream& s) {
        Unserialize(s);
    }

    void SanitizeForSerialization();
};

/** Produce a script signature using a generic signature creator. */
bool ProduceSignature(const SigningProvider& provider, const BaseSignatureCreator& creator, const CScript& scriptPubKey, SignatureData& sigdata);

/** Produce a script signature for a transaction. */
bool SignSignature(const SigningProvider &provider, const CScript& fromPubKey, CMutableTransaction& txTo, unsigned int nIn, const CAmount& amount, int nHashType);
bool SignSignature(const SigningProvider &provider, const CTransaction& txFrom, CMutableTransaction& txTo, unsigned int nIn, int nHashType);

/** Combine two script signatures using a generic signature checker, intelligently, possibly with OP_0 placeholders. */
SignatureData CombineSignatures(const CScript& scriptPubKey, const BaseSignatureChecker& checker, const SignatureData& scriptSig1, const SignatureData& scriptSig2);

/** Extract signature data from a transaction, and insert it. */
SignatureData DataFromTransaction(const CMutableTransaction& tx, unsigned int nIn);
void UpdateTransaction(CMutableTransaction& tx, unsigned int nIn, const SignatureData& data);
void UpdateInput(CTxIn& input, const SignatureData& data);

/* Check whether we know how to sign for an output like this, assuming we
 * have all private keys. While this function does not need private keys, the passed
 * provider is used to look up public keys and redeemscripts by hash.
 * Solvability is unrelated to whether we consider this output to be ours. */
bool IsSolvable(const SigningProvider& provider, const CScript& script);

#endif // BITCOIN_SCRIPT_SIGN_H
