#pragma once
#include "common/common_defs.hpp"
#include "logger/logger.hpp"

#include <filesystem>
#ifdef SSL_ON
namespace ensuressl
{
using namespace reactor;
static constexpr std::string_view trustStorePath{"/etc/ssl/certs/authority"};

constexpr const char* x509Comment = "Generated from OpenBMC service";
inline ssl::context
    getSslContext(boost::asio::ssl::context::method servorclient,
                  const std::string& sslPemFile)
{
    boost::asio::ssl::context mSslContext{servorclient};

    mSslContext.set_options(boost::asio::ssl::context::default_workarounds |
                            boost::asio::ssl::context::no_sslv2 |
                            boost::asio::ssl::context::no_sslv3 |
                            boost::asio::ssl::context::single_dh_use |
                            boost::asio::ssl::context::no_tlsv1 |
                            boost::asio::ssl::context::no_tlsv1_1);

    // BIG WARNING: This needs to stay disabled, as there will always be
    // unauthenticated endpoints
    // mSslContext.set_verify_mode(boost::asio::ssl::verify_peer);

    SSL_CTX_set_options(mSslContext.native_handle(), SSL_OP_NO_RENEGOTIATION);

    REACTOR_LOG_DEBUG("Using default TrustStore location: {}",
                      trustStorePath.data());
    mSslContext.add_verify_path(trustStorePath.data());

    mSslContext.use_certificate_file(sslPemFile,
                                     boost::asio::ssl::context::pem);
    mSslContext.use_private_key_file(sslPemFile,
                                     boost::asio::ssl::context::pem);

    // Set up EC curves to auto (boost asio doesn't have a method for this)
    // There is a pull request to add this.  Once this is included in an asio
    // drop, use the right way
    // http://stackoverflow.com/questions/18929049/boost-asio-with-ecdsa-certificate-issue
    if (SSL_CTX_set_ecdh_auto(mSslContext.native_handle(), 1) != 1)
    {}

    // Mozilla intermediate cipher suites v5.7
    // Sourced from: https://ssl-config.mozilla.org/guidelines/5.7.json
    const char* mozillaIntermediate = "ECDHE-ECDSA-AES128-GCM-SHA256:"
                                      "ECDHE-RSA-AES128-GCM-SHA256:"
                                      "ECDHE-ECDSA-AES256-GCM-SHA384:"
                                      "ECDHE-RSA-AES256-GCM-SHA384:"
                                      "ECDHE-ECDSA-CHACHA20-POLY1305:"
                                      "ECDHE-RSA-CHACHA20-POLY1305:"
                                      "DHE-RSA-AES128-GCM-SHA256:"
                                      "DHE-RSA-AES256-GCM-SHA384:"
                                      "DHE-RSA-CHACHA20-POLY1305";

    if (SSL_CTX_set_cipher_list(mSslContext.native_handle(),
                                mozillaIntermediate) != 1)
    {
        REACTOR_LOG_ERROR("Error setting cipher list");
    }
    return mSslContext;
}
inline std::optional<std::string> sslGetSubjectName(X509* peerCert,
                                                    auto&& extractor)
{
    std::string sslUser;
    // Extract username contained in CommonName
    sslUser.resize(256, '\0');

    auto status = extractor(X509_get_subject_name(peerCert), sslUser.data(),
                            sslUser.size());
    if (status == -1)
    {
        REACTOR_LOG_DEBUG("TLS cannot get Subject from certificate");
        return std::nullopt;
    }

    size_t lastChar = sslUser.find('\0');
    if (lastChar == std::string::npos || lastChar == 0)
    {
        REACTOR_LOG_DEBUG("Invalid Subject Name");
        return std::nullopt;
    }
    sslUser.resize(lastChar);
    return sslUser;
}
inline std::optional<std::string>
    verifyMtlsUser(const boost::asio::ip::address& clientIp,
                   boost::asio::ssl::verify_context& ctx)
{
    X509_STORE_CTX* cts = ctx.native_handle();
    if (cts == nullptr)
    {
        REACTOR_LOG_DEBUG("Cannot get native TLS handle.");
        return std::nullopt;
    }

    // Get certificate
    X509* peerCert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    if (peerCert == nullptr)
    {
        REACTOR_LOG_DEBUG("Cannot get current TLS certificate.");
        return std::nullopt;
    }

    // Check if certificate is OK
    int ctxError = X509_STORE_CTX_get_error(cts);
    if (ctxError != X509_V_OK)
    {
        REACTOR_LOG_INFO("Last TLS error is: {}", ctxError);
        return std::nullopt;
    }

    // Check that we have reached final certificate in chain
    int32_t depth = X509_STORE_CTX_get_error_depth(cts);
    if (depth != 0)
    {
        REACTOR_LOG_DEBUG(
            "Certificate verification in progress (depth {}), waiting to reach final depth",
            depth);
        return std::nullopt;
    }

    REACTOR_LOG_DEBUG("Certificate verification of final depth");

    if (X509_check_purpose(peerCert, X509_PURPOSE_SSL_CLIENT, 0) != 1)
    {
        REACTOR_LOG_DEBUG(
            "Chain does not allow certificate to be used for SSL client authentication");
        return std::nullopt;
    }
    return sslGetSubjectName(peerCert,
                             [](X509_NAME* name, char* buffer, size_t size) {
        return X509_NAME_get_text_by_NID(name, NID_commonName, buffer, size);
    });
}
void displayVarificationError(boost::asio::ssl::verify_context& ctx)
{
    X509_STORE_CTX* cts = ctx.native_handle();
    int32_t depth = X509_STORE_CTX_get_error_depth(cts);
    int32_t err = X509_STORE_CTX_get_error(cts);
    X509* cert = X509_STORE_CTX_get_current_cert(cts);
    auto data =
        sslGetSubjectName(cert, [](X509_NAME* name, char* buffer, size_t size) {
        auto buf = X509_NAME_oneline(name, buffer, size);
        return (buf == nullptr) ? -1 : 1;
    });

    REACTOR_LOG_ERROR("verify error:num={}::{}::{}::{}",
                      X509_STORE_CTX_get_error(cts),
                      X509_verify_cert_error_string(err), depth, data.value());
}

struct OpenSSLGenerator
{
    uint8_t operator()()
    {
        uint8_t index = 0;
        int rc = RAND_bytes(&index, sizeof(index));
        if (rc != opensslSuccess)
        {
            std::cerr << "Cannot get random number\n";
            err = true;
        }

        return index;
    }

    static constexpr uint8_t max()
    {
        return std::numeric_limits<uint8_t>::max();
    }
    static constexpr uint8_t min()
    {
        return std::numeric_limits<uint8_t>::min();
    }

    bool error() const
    {
        return err;
    }

    // all generators require this variable
    using result_type = uint8_t;

  private:
    // RAND_bytes() returns 1 on success, 0 otherwise. -1 if bad function
    static constexpr int opensslSuccess = 1;
    bool err = false;
};
void write_key_to_file(EVP_PKEY* pkey, const char* filename)
{
    std::filesystem::path path(filename);
    std::string keypath = path.parent_path().string() + "/key.key";
    FILE* fp = fopen(keypath.c_str(), "w");
    if (!fp)
    {
        // Handle error
        return;
    }

    if (!PEM_write_PrivateKey(fp, pkey, NULL, NULL, 0, NULL, NULL))
    {
        // Handle error
    }

    fclose(fp);
}
inline EVP_PKEY* createEcKey()
{
    EVP_PKEY* pKey = nullptr;

#if (OPENSSL_VERSION_NUMBER < 0x30000000L)
    int eccgrp = 0;
    eccgrp = OBJ_txt2nid("secp384r1");

    EC_KEY* myecc = EC_KEY_new_by_curve_name(eccgrp);
    if (myecc != nullptr)
    {
        EC_KEY_set_asn1_flag(myecc, OPENSSL_EC_NAMED_CURVE);
        EC_KEY_generate_key(myecc);
        pKey = EVP_PKEY_new();
        if (pKey != nullptr)
        {
            if (EVP_PKEY_assign(pKey, EVP_PKEY_EC, myecc) != 0)
            {
                /* pKey owns myecc from now */
                if (EC_KEY_check_key(myecc) <= 0)
                {
                    std::cerr << "EC_check_key failed.\n";
                }
            }
        }
    }
#else
    // Create context for curve parameter generation.
    std::unique_ptr<EVP_PKEY_CTX, decltype(&::EVP_PKEY_CTX_free)> ctx{
        EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), &::EVP_PKEY_CTX_free};
    if (!ctx)
    {
        return nullptr;
    }

    // Set up curve parameters.
    EVP_PKEY* params = nullptr;
    if ((EVP_PKEY_paramgen_init(ctx.get()) <= 0) ||
        (EVP_PKEY_CTX_set_ec_param_enc(ctx.get(), OPENSSL_EC_NAMED_CURVE) <=
         0) ||
        (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx.get(), NID_secp384r1) <=
         0) ||
        (EVP_PKEY_paramgen(ctx.get(), &params) <= 0))
    {
        return nullptr;
    }

    // Set up RAII holder for params.
    std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)> pparams{
        params, &::EVP_PKEY_free};

    // Set new context for key generation, using curve parameters.
    ctx.reset(EVP_PKEY_CTX_new_from_pkey(nullptr, params, nullptr));
    if (!ctx || (EVP_PKEY_keygen_init(ctx.get()) <= 0))
    {
        return nullptr;
    }

    // Generate key.
    if (EVP_PKEY_keygen(ctx.get(), &pKey) <= 0)
    {
        return nullptr;
    }
#endif

    return pKey;
}
inline void initOpenssl()
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    RAND_load_file("/dev/urandom", 1024);
#endif
}
inline int addExt(X509* cert, int nid, const char* value)
{
    X509_EXTENSION* ex = nullptr;
    X509V3_CTX ctx{};
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    ex = X509V3_EXT_conf_nid(nullptr, &ctx, nid, const_cast<char*>(value));
    if (ex == nullptr)
    {
        REACTOR_LOG_ERROR("Error: In X509V3_EXT_conf_nidn: {}", value);
        return -1;
    }
    X509_add_ext(cert, ex, -1);
    X509_EXTENSION_free(ex);
    return 0;
}
inline void generateSslCertificate(const std::string& filepath,
                                   const std::string& cn)
{
    FILE* pFile = nullptr;
    REACTOR_LOG_DEBUG("Generating new keys");
    initOpenssl();

    REACTOR_LOG_DEBUG("Generating EC key");
    EVP_PKEY* pPrivKey = createEcKey();
    if (pPrivKey != nullptr)
    {
        write_key_to_file(pPrivKey, filepath.c_str());
        REACTOR_LOG_DEBUG("Generating x509 Certificate");
        // Use this code to directly generate a certificate
        X509* x509 = X509_new();
        if (x509 != nullptr)
        {
            // get a random number from the RNG for the certificate serial
            // number If this is not random, regenerating certs throws browser
            // errors
            OpenSSLGenerator gen;
            std::uniform_int_distribution<int> dis(
                1, std::numeric_limits<int>::max());
            int serial = dis(gen);

            ASN1_INTEGER_set(X509_get_serialNumber(x509), serial);

            // not before this moment
            X509_gmtime_adj(X509_get_notBefore(x509), 0);
            // Cert is valid for 10 years
            X509_gmtime_adj(X509_get_notAfter(x509),
                            60L * 60L * 24L * 365L * 10L);

            // set the public key to the key we just generated
            X509_set_pubkey(x509, pPrivKey);

            // get the subject name
            X509_NAME* name = X509_get_subject_name(x509);

            using x509String = const unsigned char;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            x509String* country = reinterpret_cast<x509String*>("US");
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            x509String* company = reinterpret_cast<x509String*>("OpenBMC");
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            x509String* cnStr = reinterpret_cast<x509String*>(cn.c_str());

            X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, country, -1, -1,
                                       0);
            X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, company, -1, -1,
                                       0);
            X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, cnStr, -1, -1,
                                       0);
            // set the CSR options
            X509_set_issuer_name(x509, name);

            X509_set_version(x509, 2);
            addExt(x509, NID_basic_constraints, ("critical,CA:TRUE"));
            addExt(x509, NID_subject_alt_name, ("DNS:" + cn).c_str());
            addExt(x509, NID_subject_key_identifier, ("hash"));
            addExt(x509, NID_authority_key_identifier, ("keyid"));
            addExt(x509, NID_key_usage, ("digitalSignature, keyEncipherment"));
            addExt(x509, NID_ext_key_usage, ("serverAuth"));
            addExt(x509, NID_netscape_comment, (x509Comment));

            // Sign the certificate with our private key
            X509_sign(x509, pPrivKey, EVP_sha256());

            pFile = fopen(filepath.c_str(), "wt");

            if (pFile != nullptr)
            {
                PEM_write_PrivateKey(pFile, pPrivKey, nullptr, nullptr, 0,
                                     nullptr, nullptr);

                PEM_write_X509(pFile, x509);
                fclose(pFile);
                pFile = nullptr;
            }

            X509_free(x509);
        }

        EVP_PKEY_free(pPrivKey);
        pPrivKey = nullptr;
    }

    // cleanup_openssl();
}
inline bool isTrustChainError(int errnum)
{
    return (errnum == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT) ||
           (errnum == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN) ||
           (errnum == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY) ||
           (errnum == X509_V_ERR_CERT_UNTRUSTED) ||
           (errnum == X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE);
}
inline bool validateCertificate(X509* const cert)
{
    // Create an empty X509_STORE structure for certificate validation.
    X509_STORE* x509Store = X509_STORE_new();
    if (x509Store == nullptr)
    {
        REACTOR_LOG_ERROR("Error occurred during X509_STORE_new call");
        return false;
    }

    // Load Certificate file into the X509 structure.
    X509_STORE_CTX* storeCtx = X509_STORE_CTX_new();
    if (storeCtx == nullptr)
    {
        REACTOR_LOG_ERROR("Error occurred during X509_STORE_CTX_new call");
        X509_STORE_free(x509Store);
        return false;
    }

    int errCode = X509_STORE_CTX_init(storeCtx, x509Store, cert, nullptr);
    if (errCode != 1)
    {
        REACTOR_LOG_ERROR("Error occurred during X509_STORE_CTX_init call");
        X509_STORE_CTX_free(storeCtx);
        X509_STORE_free(x509Store);
        return false;
    }

    errCode = X509_verify_cert(storeCtx);
    if (errCode == 1)
    {
        REACTOR_LOG_INFO("Certificate verification is success");
        X509_STORE_CTX_free(storeCtx);
        X509_STORE_free(x509Store);
        return true;
    }
    if (errCode == 0)
    {
        errCode = X509_STORE_CTX_get_error(storeCtx);
        X509_STORE_CTX_free(storeCtx);
        X509_STORE_free(x509Store);
        if (isTrustChainError(errCode))
        {
            REACTOR_LOG_DEBUG("Ignoring Trust Chain error. Reason: {}",
                              X509_verify_cert_error_string(errCode));
            return true;
        }
        REACTOR_LOG_ERROR("Certificate verification failed. Reason: {}",
                          X509_verify_cert_error_string(errCode));
        return false;
    }

    REACTOR_LOG_ERROR(
        "Error occurred during X509_verify_cert call. ErrorCode: {}", errCode);
    X509_STORE_CTX_free(storeCtx);
    X509_STORE_free(x509Store);
    return false;
}
inline bool verifyOpensslKeyCert(const std::string& filepath)
{
    bool privateKeyValid = false;
    bool certValid = false;

    std::cout << "Checking certs in file " << filepath << "\n";

    FILE* file = fopen(filepath.c_str(), "r");
    if (file != nullptr)
    {
        EVP_PKEY* pkey = PEM_read_PrivateKey(file, nullptr, nullptr, nullptr);
        if (pkey != nullptr)
        {
#if (OPENSSL_VERSION_NUMBER < 0x30000000L)
            RSA* rsa = EVP_PKEY_get1_RSA(pkey);
            if (rsa != nullptr)
            {
                std::cout << "Found an RSA key\n";
                if (RSA_check_key(rsa) == 1)
                {
                    privateKeyValid = true;
                }
                else
                {
                    std::cerr << "Key not valid error number "
                              << ERR_get_error() << "\n";
                }
                RSA_free(rsa);
            }
            else
            {
                EC_KEY* ec = EVP_PKEY_get1_EC_KEY(pkey);
                if (ec != nullptr)
                {
                    std::cout << "Found an EC key\n";
                    if (EC_KEY_check_key(ec) == 1)
                    {
                        privateKeyValid = true;
                    }
                    else
                    {
                        std::cerr << "Key not valid error number "
                                  << ERR_get_error() << "\n";
                    }
                    EC_KEY_free(ec);
                }
            }
#else
            EVP_PKEY_CTX* pkeyCtx = EVP_PKEY_CTX_new_from_pkey(nullptr, pkey,
                                                               nullptr);

            if (pkeyCtx == nullptr)
            {
                std::cerr << "Unable to allocate pkeyCtx " << ERR_get_error()
                          << "\n";
            }
            else if (EVP_PKEY_check(pkeyCtx) == 1)
            {
                privateKeyValid = true;
            }
            else
            {
                std::cerr << "Key not valid error number " << ERR_get_error()
                          << "\n";
            }
#endif

            if (privateKeyValid)
            {
                // If the order is certificate followed by key in input file
                // then, certificate read will fail. So, setting the file
                // pointer to point beginning of file to avoid certificate and
                // key order issue.
                fseek(file, 0, SEEK_SET);

                X509* x509 = PEM_read_X509(file, nullptr, nullptr, nullptr);
                if (x509 == nullptr)
                {
                    std::cout << "error getting x509 cert " << ERR_get_error()
                              << "\n";
                }
                else
                {
                    certValid = validateCertificate(x509);
                    X509_free(x509);
                }
            }

#if (OPENSSL_VERSION_NUMBER > 0x30000000L)
            EVP_PKEY_CTX_free(pkeyCtx);
#endif
            EVP_PKEY_free(pkey);
        }
        fclose(file);
    }
    return certValid;
}
inline void ensureOpensslKeyPresentAndValid(const std::string& filepath)
{
    bool pemFileValid = false;

    pemFileValid = verifyOpensslKeyCert(filepath);

    if (!pemFileValid)
    {
        REACTOR_LOG_INFO("Error in verifying signature, regenerating\n");
        generateSslCertificate(filepath, "testhost");
    }
}
inline boost::asio::ssl::context
    loadCertificate(boost::asio::ssl::context::method servOrClient,
                    const std::string& certDir)
{
    namespace fs = std::filesystem;

    fs::path certPath = certDir;
    // if path does not exist create the path so that
    // self signed certificate can be created in the
    // path
    if (!fs::exists(certPath))
    {
        fs::create_directories(certPath);
    }
    fs::path certFile = certPath / "client-cert.pem";
    REACTOR_LOG_INFO("Building SSL Context file={}", certFile.string());
    std::string sslPemFile(certFile);
    ensureOpensslKeyPresentAndValid(sslPemFile);
    boost::asio::ssl::context sslContext = getSslContext(servOrClient,
                                                         sslPemFile);
    return sslContext;
}
inline bool tlsVerifyCallback(bool preverified,
                              boost::asio::ssl::verify_context& ctx)
{
    if (preverified)
    {
        boost::asio::ip::address ipAddress;
        // if (getClientIp(ipAddress))
        // {
        //     return true;
        // }

        auto mtlsSession = verifyMtlsUser(ipAddress, ctx);
        if (mtlsSession)
        {
            REACTOR_LOG_DEBUG("Generating TLS USER: {}", mtlsSession.value());
        }
        return true;
    }

    displayVarificationError(ctx);
    return validateCertificate(
        X509_STORE_CTX_get_current_cert(ctx.native_handle()));
}
inline void setSessionId(auto& adaptor, const std::string& id)
{
    const char* cStr = id.c_str();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* idC = reinterpret_cast<const unsigned char*>(cStr);
    int ret = SSL_set_session_id_context(
        adaptor.native_handle(), idC, static_cast<unsigned int>(id.length()));
    if (ret == 0)
    {
        REACTOR_LOG_ERROR("failed to set SSL id");
    }
}

inline void prepareMutualTls(auto& context, std::string_view trustStorePath)
{
    std::error_code error;
    std::filesystem::path caPath(trustStorePath);
    auto caAvailable = !std::filesystem::is_empty(caPath, error);
    caAvailable = caAvailable && !error;
    if (caAvailable)
    {
        context.set_verify_mode(boost::asio::ssl::verify_peer);
        context.add_verify_path(trustStorePath.data());
    }

    context.set_verify_callback(tlsVerifyCallback);
}
} // namespace ensuressl
#endif
