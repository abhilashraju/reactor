#include <openssl/evp.h>
#include <string.h>

#include <memory>

class AESCipher
{
  private:
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&::EVP_CIPHER_CTX_free)> ctx;

  public:
    AESCipher(std::string_view key) :
        ctx(EVP_CIPHER_CTX_new(), ::EVP_CIPHER_CTX_free)
    {
        EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_cbc(), NULL,
                           (unsigned char*)key.data(), NULL);
    }

    std::string encrypt(std::string_view plaintext)
    {
        int len;
        std::string ciphertext(plaintext.length() + EVP_MAX_BLOCK_LENGTH, '\0');

        EVP_EncryptUpdate(
            ctx.get(), reinterpret_cast<unsigned char*>(&ciphertext[0]), &len,
            reinterpret_cast<const unsigned char*>(plaintext.data()),
            plaintext.length());

        int padding_len;
        EVP_EncryptFinal_ex(ctx.get(),
                            reinterpret_cast<unsigned char*>(&ciphertext[len]),
                            &padding_len);

        ciphertext.resize(len + padding_len);
        return ciphertext;
    }

    std::string decrypt(std::string_view ciphertext)
    {
        int len;
        std::string plaintext(ciphertext.length(), '\0');

        EVP_DecryptUpdate(
            ctx.get(), reinterpret_cast<unsigned char*>(&plaintext[0]), &len,
            reinterpret_cast<const unsigned char*>(ciphertext.data()),
            ciphertext.length());

        int padding_len;
        EVP_DecryptFinal_ex(ctx.get(),
                            reinterpret_cast<unsigned char*>(&plaintext[len]),
                            &padding_len);

        plaintext.resize(len + padding_len);
        return plaintext;
    }
    static std::string generateKey()
    {
        std::string key(EVP_CIPHER_key_length(EVP_aes_256_cbc()), '\0');
        if (RAND_bytes((unsigned char*)(key.data()), key.length()) != 1)
        {
            REACTOR_LOG_ERROR("Could not generate key");
            throw std::runtime_error("Could not generate key");
        }
        return key;
    }
};
