#pragma once
#include <openssl/dh.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <string.h>

#include <fstream>

namespace reactor
{
class DH_AES
{
  private:
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&::EVP_CIPHER_CTX_free)> ctx;
    DH* dh;
    unsigned char* key;
    DH* get_dh2048(std::string_view p, std::string_view g)
    {
        DH* dh = DH_new();
        if (dh == NULL)
            return NULL;
        dh->p = BN_bin2bn((const unsigned char*)p.data(), p.size(), NULL);
        dh->g = BN_bin2bn((const unsigned char*)g.data(), g.size(), NULL);
        if ((dh->p == NULL) || (dh->g == NULL))
            return NULL;
        return dh;
    }
    void makeKey(std::string_view p, std::string_view g,
                 std::string_view other_public_key)
    {
        dh = get_dh2048(p, g);
        if (dh == NULL)
            throw std::runtime_error("Could not create DH object");
        DH_generate_key(dh);
        key = OPENSSL_malloc(DH_size(dhparams));
        if (key == NULL)
            throw std::runtime_error("Could not create key");
        DH_compute_key(key, other_public_key, dh);
    }

  public:
    DH_AES(std::string_view p, std::string_view g,
           std::string_view other_public_key)
    {
        makeKey(p, g, other_public_key);
    }

    ~DH_AES()
    {
        DH_free(dh);
        OPENSSL_free(key);
    }
    std::string encrypt(std::string_view plaintext)
    {
        unsigned char iv[16];
        RAND_bytes(iv, sizeof(iv)); // Generate a random IV

        EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_cbc(), NULL, key, iv);
        int len{0};
        std::string ciphertext(plaintext.length() +
                               EVP_CIPHER_CTX_block_size(ctx.get()));
        EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &len, plaintext.data(),
                          plaintext.length());
        int ciphertext_len = len;
        EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + len, &len);
        ciphertext_len += len;
        ciphertext.resize(ciphertext_len);
        return ciphertext;
    }

    std::string decrypt(std::string_view ciphertext, std::string_view iv)
    {
        EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_cbc(), NULL, key,
                           (unsigned char*)iv.data());
        std::string plaintext(ciphertext.length());
        int len{0};
        EVP_DecryptUpdate(ctx.get(), plaintext, &len, ciphertext.data(),
                          ciphertext.length());
        int plaintext_len = len;
        EVP_DecryptFinal_ex(ctx.get(), plaintext + len, &len);
        plaintext_len += len;
        plaintext.resize(plaintext_len);
        return plaintext;
    }
    void save_key(const char* filename)
    {
        FILE* file = fopen(filename, "w");
        if (!file)
            throw std::runtime_error("Could not open file for writing");
        PEM_write_DHparams(file, dh);
        fclose(file);
    }

    void load_key(const char* filename)
    {
        FILE* file = fopen(filename, "r");
        if (!file)
            throw std::runtime_error("Could not open file for reading");
        PEM_read_DHparams(file, &dh, NULL, NULL);
        fclose(file);
        DH_compute_key(key, other_public_key, dh);
    }
};
} // namespace reactor
