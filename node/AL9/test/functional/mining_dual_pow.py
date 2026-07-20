#!/usr/bin/env python3
# Copyright (c) 2026 The HobbyHash Core developers
# V5 dual-PoW regtest functional checks (GBT powalgo routing).

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MiningDualPowTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]
        # Regtest V5 activation is height 150 (chainparams).
        self.generate(node, 149)
        info = node.getmininginfo()
        assert_equal(info.get("powalgo"), "sha256")
        assert "bits_sha" not in info

        self.generate(node, 1)
        info = node.getmininginfo()
        assert_equal(info["blocks"], 150)
        assert_equal(info["powalgo"], "sha256")
        assert_equal(info["heightmod6"], 150 % 6)
        assert "bits_sha" in info
        assert "bits_gpu" in info

        templates = []
        for _ in range(6):
            tmpl = node.getblocktemplate({"rules": ["segwit"]})
            templates.append(tmpl["powalgo"])
            self.generate(node, 1)

        self.log.info("powalgo cycle: %s", templates)
        assert_equal(templates[0:3], ["sha256", "sha256", "sha256"])
        assert_equal(templates[3:6], ["kawpow", "kawpow", "kawpow"])

        gpu_tmpl = node.getblocktemplate({"rules": ["segwit"]})
        if gpu_tmpl["powalgo"] == "kawpow":
            assert_equal(gpu_tmpl["noncerange"], "0000000000000000ffffffffffffffff")


if __name__ == "__main__":
    MiningDualPowTest().main()
