#include "keystore.h"
#include "core/proto.h"
#include <boost/filesystem.hpp>
#include <stdexcept>
#include <stdio.h>

namespace beam {

namespace {

using PrivKey = ECC::Scalar::Native;
using KeyPairs = std::map<PubKey, ECC::NoLeak<PrivKey>>;
using Nonce = ECC::Scalar::Native;

void gen_nonce(Nonce& nonce) {
    ECC::Scalar sc;
    uint64_t seed;

    // here we want to read as little as possible from slow sources, TODO: review this
    ECC::GenRandom(&seed, 8);
    ECC::Hash::Processor() << seed >> sc.m_Value;

    nonce.Import(sc);
}

void aes_decrypt(void* buffer, size_t bufferLen, const void* password, size_t passwordLen) {
    //TODO
}

void aes_encrypt(void* buffer, size_t bufferLen, const void* password, size_t passwordLen) {
    //TODO
}

struct AutoClose {
    FILE* f=0;
    ~AutoClose() { if(f) fclose(f); }
};

void read_keystore_file(KeyPairs& out, const std::string& fileName, const void* password, size_t passwordLen) {
    AutoClose a;

    a.f = fopen(fileName.c_str(), "a+b");
    if (!a.f) {
        throw std::runtime_error(std::string("keystore: cannot open file ") + fileName);
    }

    fseek(a.f, 0, SEEK_END);
    size_t size = ftell(a.f);

    if (size == 0) {
        return;
    }

    static const size_t MAX_FILE_SIZE = 64*2000;

    if ((size % 64) != 0 || size > MAX_FILE_SIZE) {
        fclose(a.f);
        throw std::runtime_error(std::string("keystore: invalid file size: ") + fileName);
    }

    void* buffer = alloca(size);

    rewind(a.f);
    auto bytesRead = fread(buffer, 1, size, a.f);
    if (bytesRead != size) {
        throw std::runtime_error(std::string("keystore: file read error: ") + fileName);
    }

    aes_decrypt(buffer, size, password, passwordLen);

    const uint8_t* p = (const uint8_t*)buffer;
    const uint8_t* end = p + size;
    PubKey pubKey;
    for (; p<end; p += 64) {
        memcpy(pubKey.m_pData, p, 32);
        memcpy(&(out[pubKey].V), p+32, 32);
    }
}

void write_keystore_file(const KeyPairs& in, const std::string& fileName, const void* password, size_t passwordLen) {
    std::string newFileName = fileName + ".new";

    size_t size = in.size() * 64;

    {
        AutoClose a;

        a.f = fopen(newFileName.c_str(), "w+b");
        if (!a.f) {
            throw std::runtime_error(std::string("keystore: cannot open file ") + newFileName);
        }

        if (size == 0)
            return;

        void* buffer = alloca(size);

        aes_encrypt(buffer, size, password, passwordLen);

        if (size != fwrite(buffer, 1, size, a.f)) {
            throw std::runtime_error(std::string("keystore: cannot write file ") + newFileName);
        }
    }

    boost::filesystem::rename(newFileName, fileName);
}

} //namespace

class LocalFileKeystore : public IKeyStore {
public:
    LocalFileKeystore(const IKeyStore::Options& options, const void* password, size_t passwordLen) :
        _fileName(options.fileName)
    {
        bool allEnabled = (options.flags & Options::enable_all_keys) != 0;
        if (allEnabled) {
            read_keystore_file(_keyPairs, _fileName, password, passwordLen);
        } else {
            enable_keys_impl(options.enableKeys, password, passwordLen);
        }
    }

private:
    void enable_keys_impl(const std::set<PubKey>& enableKeys, const void* password, size_t passwordLen) {
        _keyPairs.clear();
        if (enableKeys.empty())
            return;
        read_keystore_file(_keyPairs, _fileName, password, passwordLen);
        if (_keyPairs.empty())
            return;
        std::set<PubKey> toBeErased;
        for (const auto& p : _keyPairs) {
            if (enableKeys.count(p.first) == 0) {
                toBeErased.insert(p.first);
            }
        }
        for (const auto& k : toBeErased) {
            _keyPairs.erase(k);
        }
    }

    void gen_keypair(PubKey& pubKey, const void* password, size_t passwordLen, bool enable) override {
        if (_keyPairs.count(pubKey)) return;
        ECC::NoLeak<PrivKey> privKey;
        gen_nonce(privKey.V);
        proto::Sk2Pk(pubKey, privKey.V);
        memcpy(&(_keyPairs[pubKey].V), &privKey.V, 32);
        write_keystore_file(_keyPairs, _fileName, password, passwordLen);
        if (!enable) {
            _keyPairs.erase(pubKey);
        }
    }

    void get_enabled_keys(std::set<PubKey>& enabledKeys) override {
        enabledKeys.clear();
        for (const auto& p : _keyPairs) {
            enabledKeys.insert(p.first);
        }
    }

    void enable_keys(const std::set<PubKey>& enableKeys, const void* password, size_t passwordLen) override {
        enable_keys_impl(enableKeys, password, passwordLen);
    }

    void disable_key(const PubKey& pubKey, bool erasePermanently, const void* password, size_t passwordLen) override {
        _keyPairs.erase(pubKey);
        if (erasePermanently) {
            write_keystore_file(_keyPairs, _fileName, password, passwordLen);
        }
    }

    bool encrypt(ByteBuffer& out, const io::SerializedMsg& in, const PubKey& pubKey) override {
        Nonce nonce;
        gen_nonce(nonce);
        io::SharedBuffer msg = io::normalize(in, false);
        return proto::BbsEncrypt(out, pubKey, nonce, msg.data, msg.size);
    }

    bool decrypt(uint8_t*& out, uint32_t& size, ByteBuffer& buffer, const PubKey& pubKey) override {
        auto it = _keyPairs.find(pubKey);
        if (it == _keyPairs.end()) {
            return false;
        }
        out = &buffer.at(0);
        size = buffer.size();
        return proto::BbsDecrypt(out, size, (PrivKey&)it->second.V);
    }

    std::string _fileName;
    KeyPairs _keyPairs;
};

IKeyStore::Ptr IKeyStore::create(const IKeyStore::Options& options, const void* password, size_t passwordLen) {
    static const std::string errMsgPrefix("keystore create: ");

    Ptr ptr;

    if (options.flags & Options::local_file) {
        if (options.fileName.empty() || passwordLen == 0) {
            throw std::runtime_error(errMsgPrefix + "empty file name or key");
        }
        ptr = std::make_shared<LocalFileKeystore>(options, password, passwordLen);

    } else {
        throw std::runtime_error(errMsgPrefix + "invalid options");
    }

    return ptr;
}

} //namespace
