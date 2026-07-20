// Copyright (c) 2026 The HobbyHash Core developers
// V4 replay-protection unit tests (SIGHASH_FORKID)

#include <consensus/amount.h>
#include <key.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/sign.h>
#include <script/script.h>
#include <uint256.h>
#include <util/translation.h>

#include <cstdio>
#include <vector>

const TranslateFn G_TRANSLATION_FUN{nullptr};

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        ++g_failures; \
    } \
} while (0)

static CMutableTransaction MakeWitnessTx()
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.nLockTime = 0;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].prevout = COutPoint(Txid::FromUint256(uint256::ONE), 0);
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    tx.vout[0].nValue = 45 * COIN;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    return tx;
}

static void TestReplayAwareHashType()
{
    CHECK(ReplayAwareSignHashType(SIGHASH_ALL, 0) == SIGHASH_ALL, "no fork id -> plain ALL");
    CHECK(ReplayAwareSignHashType(SIGHASH_ALL, 0x00007000) == (SIGHASH_ALL | SIGHASH_FORKID),
          "fork id -> ALL | FORKID");
    CHECK(ReplayAwareSignHashType(SIGHASH_DEFAULT, 0x00007000) == (SIGHASH_ALL | SIGHASH_FORKID),
          "DEFAULT at fork -> ALL | FORKID");
}

static void TestSignatureHashForkId()
{
    const CMutableTransaction mtx = MakeWitnessTx();
    const CScript scriptCode = CScript() << OP_DUP << OP_HASH160 << ParseHex("0000000000000000000000000000000000000000")
                                         << OP_EQUALVERIFY << OP_CHECKSIG;
    const CAmount amount = mtx.vout[0].nValue;

    const uint256 legacy = SignatureHash(scriptCode, mtx, 0, SIGHASH_ALL, amount, SigVersion::WITNESS_V0,
                                         nullptr, nullptr, 0);
    const uint256 forked = SignatureHash(scriptCode, mtx, 0, SIGHASH_ALL | SIGHASH_FORKID, amount, SigVersion::WITNESS_V0,
                                         nullptr, nullptr, 0x00007000);

    CHECK(legacy != forked, "FORKID sighash differs from legacy");
    CHECK(legacy != uint256::ZERO, "legacy sighash non-zero");
    CHECK(forked != uint256::ZERO, "fork sighash non-zero");
}

static void TestForkSigRequiredAtActivation()
{
    CKey key;
    key.MakeNewKey(true);
    const CPubKey pubkey = key.GetPubKey();

    CMutableTransaction mtx = MakeWitnessTx();
    const CTransaction txConst(mtx);
    const CScript scriptCode = CScript() << pubkey << OP_CHECKSIG;
    const CAmount amount = mtx.vout[0].nValue;

    const int legacy_type = SIGHASH_ALL;
    const uint256 legacy_hash = SignatureHash(scriptCode, mtx, 0, legacy_type, amount, SigVersion::WITNESS_V0,
                                              nullptr, nullptr, 0);
    std::vector<unsigned char> legacy_sig;
    CHECK(key.Sign(legacy_hash, legacy_sig), "sign legacy");
    legacy_sig.push_back(static_cast<unsigned char>(legacy_type));

    const int fork_type = SIGHASH_ALL | SIGHASH_FORKID;
    const uint256 fork_hash = SignatureHash(scriptCode, mtx, 0, fork_type, amount, SigVersion::WITNESS_V0,
                                           nullptr, nullptr, 0x00007000);
    std::vector<unsigned char> fork_sig;
    CHECK(key.Sign(fork_hash, fork_sig), "sign fork");
    fork_sig.push_back(static_cast<unsigned char>(fork_type));

    TransactionSignatureChecker legacy_checker(&txConst, 0, amount, MissingDataBehavior::FAIL, 0, false);
    TransactionSignatureChecker fork_checker(&txConst, 0, amount, MissingDataBehavior::FAIL, 0x00007000, true);

    const std::vector<unsigned char> vch_pubkey(pubkey.begin(), pubkey.end());

    CHECK(legacy_checker.CheckECDSASignature(legacy_sig, vch_pubkey, scriptCode, SigVersion::WITNESS_V0),
          "legacy sig valid under legacy rules");
    CHECK(!fork_checker.CheckECDSASignature(legacy_sig, vch_pubkey, scriptCode, SigVersion::WITNESS_V0),
          "legacy sig rejected when FORKID required");
    CHECK(fork_checker.CheckECDSASignature(fork_sig, vch_pubkey, scriptCode, SigVersion::WITNESS_V0),
          "fork sig valid when FORKID required");
    CHECK(!legacy_checker.CheckECDSASignature(fork_sig, vch_pubkey, scriptCode, SigVersion::WITNESS_V0),
          "fork sig invalid under legacy rules");
}

int main()
{
    ECC_Context ecc;
    TestReplayAwareHashType();
    TestSignatureHashForkId();
    TestForkSigRequiredAtActivation();

    if (g_failures == 0) {
        std::printf("PASS: v4_fork_replay_tests (all checks ok)\n");
        return 0;
    }
    std::fprintf(stderr, "FAIL: v4_fork_replay_tests (%d failures)\n", g_failures);
    return 1;
}
