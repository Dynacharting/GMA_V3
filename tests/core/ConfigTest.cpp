#include "krn/util/Config.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>

using namespace krn::util;

TEST(ConfigTest, LoadFromFile) {
    const char* path = "test_config.ini";
    {
        std::ofstream f(path);
        f << "threadPoolSize=3\n"
          << "maxSymbols=50\n"
          << "maxFieldsPerSymbol=7\n"
          << "metricsIntervalSec=5\n";
    }
    Config cfg;
    EXPECT_TRUE(cfg.loadFromFile(path));
    EXPECT_EQ(cfg.threadPoolSize, 3);
    EXPECT_EQ(cfg.maxSymbols, 50);
    EXPECT_EQ(cfg.maxFieldsPerSymbol, 7);
    EXPECT_EQ(cfg.metricsIntervalSec, 5);
    std::remove(path);
}

TEST(ConfigTest, LoadIgnoresComments) {
    const char* path = "test_config_comments.ini";
    {
        std::ofstream f(path);
        f << "# This is a comment\n"
          << "; So is this\n"
          << "maxSymbols=5\n"
          << "\n"
          << "maxFieldsPerSymbol=30\n";
    }
    Config cfg;
    EXPECT_TRUE(cfg.loadFromFile(path));
    EXPECT_EQ(cfg.maxSymbols, 5);
    EXPECT_EQ(cfg.maxFieldsPerSymbol, 30);
    std::remove(path);
}

TEST(ConfigTest, LoadIgnoresUnknownKeys) {
    const char* path = "test_config_unknown.ini";
    {
        std::ofstream f(path);
        f << "unknownKey=999\n"
          << "maxSymbols=7\n";
    }
    Config cfg;
    EXPECT_TRUE(cfg.loadFromFile(path));
    EXPECT_EQ(cfg.maxSymbols, 7);
    // Unknown keys should be silently ignored; defaults preserved for unset keys
    EXPECT_EQ(cfg.maxFieldsPerSymbol, 200);
    std::remove(path);
}

TEST(ConfigTest, ReturnsFalseForMissingFile) {
    Config cfg;
    EXPECT_FALSE(cfg.loadFromFile("nonexistent_file_12345.ini"));
}

// ENC-1008 — the derived-atomic key shape is INI-switchable, and defaults off.
// The default matters more than the parse: the key it controls is a
// client-visible WS `field` string, so a build that silently came up namespaced
// would break existing AtomicAccessor bindings (see docs/atomic-keys.md).
TEST(ConfigTest, AtomicKeyNamespaceByFieldDefaultsOff) {
    EXPECT_FALSE(Config{}.atomicKeyNamespaceByField);
}

TEST(ConfigTest, AtomicKeyNamespaceByFieldParsesTruthyForms) {
    for (const char* v : {"true", "1", "yes"}) {
        const char* path = "test_config_atomic_ns.ini";
        {
            std::ofstream f(path);
            f << "atomicKeyNamespaceByField=" << v << "\n";
        }
        Config cfg;
        EXPECT_TRUE(cfg.loadFromFile(path));
        EXPECT_TRUE(cfg.atomicKeyNamespaceByField) << "value: " << v;
        std::remove(path);
    }
}

TEST(ConfigTest, AtomicKeyNamespaceByFieldStaysOffForAnythingElse) {
    for (const char* v : {"false", "0", "no", ""}) {
        const char* path = "test_config_atomic_ns_off.ini";
        {
            std::ofstream f(path);
            f << "atomicKeyNamespaceByField=" << v << "\n";
        }
        Config cfg;
        EXPECT_TRUE(cfg.loadFromFile(path));
        EXPECT_FALSE(cfg.atomicKeyNamespaceByField) << "value: '" << v << "'";
        std::remove(path);
    }
}
