// SPDX-License-Identifier: GPL-2.0-only
/*
 * EcoNet EN7523 NPU init driver.
 *
 * Reverse-engineered from the vendor 4.4.115 vmlinux / npu.ko.
 * This is a best-effort translation; register offsets come from the
 * stock disassembly of ecnt_npu_drv_probe / boot_npu_core /
 * set_npu_needed_info.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <asm/cacheflush.h>

struct econet_npu {
	struct device *dev;
	void __iomem *base;		/* 0x1e800000, 384k sram / reg */
	void __iomem *sram;		/* 0x1e900000, NPU register block */
	phys_addr_t rsv_phys;
	void *rsv_vaddr;		/* 0x84000000, npu_rv32 binary */
	size_t rsv_size;
	u32 irq[7];

	struct clk *clk;
	struct reset_control *rst;
	u32 token;

	u32 pkt_buf_addr;		/* 0x85000000, NPU packet buffer */
	void *pkt_vaddr;

	/* WIFI_MAIL payload must stay mapped while the NPU processes it. */
	u8 *mbox_payload;
	dma_addr_t mbox_dma;
};

/*
 * Offsets into the 0x1e900000 block, from the 4.4 disassembly.
 * These are the values the stock airoha ecnt-npu driver uses.
 */
#define NPU_BOOT_REG		0x308000
#define NPU_BOOT_CONFIG		0x308004
#define NPU_BOOT_ADDR(core)	(0x308020 + (core) * 4)

#define NPU_INFO_REG		0x30c000
#define NPU_INFO_FWADDR		0x168
#define NPU_INFO_L2CSIZE	0x16c
#define NPU_INFO_ISFPGA		0x170

static int econet_npu_load_to_rsv(struct econet_npu *npu,
				   const char *name, size_t max, size_t off)
{
	const struct firmware *fw;
	int ret;
	phys_addr_t phys = npu->rsv_phys + off;

	ret = request_firmware(&fw, name, npu->dev);
	if (ret)
		return ret;
	if (fw->size > max) {
		dev_err(npu->dev, "%s size %zu > %zu\n", name, fw->size, max);
		release_firmware(fw);
		return -E2BIG;
	}
	memcpy((u8 *)npu->rsv_vaddr + off, fw->data, fw->size);
	wmb();
	if (fw->size < max)
		memset((u8 *)npu->rsv_vaddr + off + fw->size, 0,
		       max - fw->size);
	wmb();
	__cpuc_flush_dcache_area((u8 *)npu->rsv_vaddr + off, max);
	release_firmware(fw);
	dev_info(npu->dev, "%s loaded (%zu bytes) at %pa\n",
		 name, fw->size, &phys);
	return 0;
}

static int econet_npu_load_to_sram(struct econet_npu *npu,
				    const char *name, size_t max, size_t off)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, name, npu->dev);
	if (ret)
		return ret;
	if (fw->size > max) {
		dev_err(npu->dev, "%s size %zu > %zu\n", name, fw->size, max);
		release_firmware(fw);
		return -E2BIG;
	}
	memcpy_toio((u8 __iomem *)npu->sram + off, fw->data, fw->size);
	wmb();
	release_firmware(fw);
	dev_info(npu->dev, "%s loaded (%zu bytes) to sram+0x%zx\n",
		 name, fw->size, off);
	return 0;
}

/*
 * test_ram_b4_npu_load: the stock routine does a 0xa5/0x5a pattern test.
 * Skipped in this minimal port; the SRAM is assumed working.
 */
static int econet_npu_test_ram(struct econet_npu *npu)
{
	dev_info(npu->dev, "SRAM test skipped\n");
	return 0;
}

static int econet_npu_get_l2c_sram_size(void)
{
	/* Unknown in this port; return a small safe value. */
	return 0x10000;
}

static int econet_npu_get_is_fpga(void)
{
	return 0;
}

static void econet_npu_set_needed_info(struct econet_npu *npu)
{
	u32 fwaddr = (u32)(npu->rsv_phys + 0x200000);
	u32 l2c = (u32)econet_npu_get_l2c_sram_size();
	u32 fpga = (u32)econet_npu_get_is_fpga();

	writel(fwaddr, npu->sram + NPU_INFO_REG + NPU_INFO_FWADDR);
	mb();
	writel(l2c, npu->sram + NPU_INFO_REG + NPU_INFO_L2CSIZE);
	mb();
	writel(fpga, npu->sram + NPU_INFO_REG + NPU_INFO_ISFPGA);
	mb();
	dev_info(npu->dev, "set_npu_needed_info: fwaddr=0x%x l2c=0x%x fpga=%u\n",
		 fwaddr, l2c, fpga);
}

static void econet_npu_boot_core(struct econet_npu *npu, int core)
{
	u32 val;

	writel(npu->rsv_phys, npu->sram + NPU_BOOT_ADDR(core));
	mb();

	if (core == 0) {
		writel(1, npu->sram + NPU_BOOT_CONFIG);
		mb();
		writel(1, npu->sram + NPU_BOOT_REG);
		mb();
	} else {
		val = readl(npu->sram + NPU_BOOT_CONFIG);
		val |= (2 << core);
		writel(val, npu->sram + NPU_BOOT_CONFIG);
		mb();
		writel(2, npu->sram + NPU_BOOT_REG);
		mb();
	}
	udelay(100);
	dev_info(npu->dev, "booted npu core %d\n", core);
}

static int econet_npu_boot(struct econet_npu *npu)
{
	int i, ret;

	ret = econet_npu_test_ram(npu);
	if (ret)
		return ret;

	ret = econet_npu_load_to_rsv(npu, "econet/npu_rv32.bin", 0x200000, 0);
	if (ret)
		return ret;

	ret = econet_npu_load_to_rsv(npu, "econet/npu_data.bin", 0x100000, 0x200000);
	if (ret)
		return ret;

	ret = econet_npu_load_to_sram(npu, "econet/npu_data.bin", 0x10000, 0xbe90);
	if (ret)
		return ret;

	/* Zero the .bss area after the .data image (0xd011 - 0xd03b). */
	memset_io((u8 __iomem *)npu->sram + 0xd011, 0, 0xd03b - 0xd011);
	wmb();

	econet_npu_set_needed_info(npu);

	for (i = 0; i < 4; i++)
		econet_npu_boot_core(npu, i);

	dev_info(npu->dev, "all NPU cores booted\n");
	return 0;
}

struct npu_mbox_msg {
	u32 word0;		/* core 0..3 */
	u32 word1;		/* sub-type 0..7 */
	u32 word2;		/* per-core ctx +12 */
	u32 word3;		/* mbox 0x30 / per-core ctx +44 */
	u16 half4;		/* mbox 0x34 / per-core ctx +76 */
	u16 _pad0;
	u8  byte5;		/* flags */
	u8  _pad1;
	u16 _pad2;
	u32 word6;		/* timeout count */
	u32 word7;		/* per-core ctx +92 */
};

#define NPU_MBOX_BASE		0x30c000
#define NPU_MBOX_STRIDE		16

static DEFINE_SPINLOCK(npu_mbox_lock);

static int econet_npu_wifi_offload_set_pkt_buf_addr(struct econet_npu *npu)
{
	struct device *dev = npu->dev;
	struct device_node *np;
	struct resource r;
	int err;

	dev_info(dev, "set_pkt_buf_addr: finding node\n");
	np = of_find_node_by_path("/reserved-memory/npu_pkt@85000000");
	if (!np)
		np = of_find_node_by_name(NULL, "npu_pkt");
	if (!np) {
		dev_err(dev, "npu_pkt reserved-memory node not found\n");
		return -ENODEV;
	}
	dev_info(dev, "set_pkt_buf_addr: got node\n");

	err = of_address_to_resource(np, 0, &r);
	of_node_put(np);
	if (err)
		return -EINVAL;

	npu->pkt_buf_addr = (u32)r.start;
	dev_info(dev, "set_pkt_buf_addr: before memremap start=%pa\n", &r.start);
	npu->pkt_vaddr = devm_memremap(dev, r.start, 0x1000,
				       MEMREMAP_WB);
	dev_info(dev, "set_pkt_buf_addr: after memremap\n");
	if (IS_ERR(npu->pkt_vaddr)) {
		dev_err(dev, "npu_pkt memremap failed: %pe\n",
			npu->pkt_vaddr);
		return PTR_ERR(npu->pkt_vaddr);
	}

	/* The NPU firmware dereferences npu_pkt[0] as a pointer to
	 * its own npu struct, then reads word[2] for the sram base.
	 * Build a minimal fake npu struct in the first 16 bytes of
	 * npu_pkt using the physical addresses the NPU sees. */
	dev_info(dev, "set_pkt_buf_addr: writing back-pointer\n");
	{
		u32 *np = (u32 *)npu->pkt_vaddr;

		np[0] = npu->pkt_buf_addr;
		np[1] = 0x1e800000;
		np[2] = 0x1e900000;
		np[3] = npu->rsv_phys;
		wmb();
		__cpuc_flush_dcache_area(npu->pkt_vaddr, 16);
	}

	dev_info(dev, "npu_pkt_buf_addr=%x\n", npu->pkt_buf_addr);
	return 0;
}

static int __npu_mbox_send_locked(struct econet_npu *npu, unsigned int core,
				  const struct npu_mbox_msg *msg)
{
	void __iomem *mb = npu->sram + NPU_MBOX_BASE + NPU_MBOX_STRIDE * core;
	u32 timeout = (msg->byte5 & 0x01) ? msg->word6 : 300;
	u32 sl = 0;

	if (msg->byte5 & 0x06)
		sl |= 0x20;
	if (msg->byte5 & 0x01)
		sl |= 0x01;

	writel(msg->word3, mb + 0x30);
	wmb();

	writel(msg->half4, mb + 0x34);
	wmb();

	writel(sl, mb + 0x3c);
	wmb();

	/* Kick the NPU by incrementing the token.  Avoid reading back
	 * the mbox because the running NPU can make these sram pages
	 * unavailable to the A53 host. */
	writel(++npu->token, mb + 0x38);
	wmb();

	udelay(timeout);
	return 1;
}

static int econet_npu_notify_mbox(struct econet_npu *npu)
{
	struct device *dev = npu->dev;
	u8 *payload;
	dma_addr_t dma_handle;
	struct npu_mbox_msg msg;
	int ret;

	if (!npu->pkt_vaddr) {
		dev_err(dev, "packet buffer not set\n");
		return -EINVAL;
	}

	payload = kzalloc(224, GFP_KERNEL);
	if (!payload)
		return -ENOMEM;

	payload[0] = 0x10;
	*(u32 *)(payload + 4) = 9;
	*(u32 *)(payload + 8) = npu->pkt_buf_addr;
	wmb();

	dma_handle = dma_map_single(dev, payload, 224, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_handle)) {
		dev_err(dev, "dma_map_single payload error\n");
		kfree(payload);
		return -EIO;
	}

	memset(&msg, 0, sizeof(msg));
	msg.word0 = 0;
	msg.word1 = 0;
	msg.word2 = 0;
	msg.word3 = (u32)dma_handle;
	msg.half4 = 224;
	msg.byte5 = 1;
	msg.word6 = 1000;
	msg.word7 = 0;

	spin_lock_bh(&npu_mbox_lock);
	ret = __npu_mbox_send_locked(npu, 0, &msg);
	spin_unlock_bh(&npu_mbox_lock);

	/* The NPU is still held in reset; keep the payload mapped so it
	 * can read the message after reset is released. */
	npu->mbox_payload = payload;
	npu->mbox_dma = dma_handle;

	dev_info(dev, "host_notify_npuMbox %s\n", ret ? "sent" : "timeout");
	return 0;
}

static int econet_npu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct econet_npu *npu;
	struct resource *res;
	struct device_node *mem;
	struct resource rsv;
	int i, ret;

	npu = devm_kzalloc(dev, sizeof(*npu), GFP_KERNEL);
	if (!npu)
		return -ENOMEM;
	npu->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	npu->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(npu->base))
		return PTR_ERR(npu->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	npu->sram = devm_ioremap_resource(dev, res);
	if (IS_ERR(npu->sram))
		return PTR_ERR(npu->sram);

	for (i = 0; i < 7; i++) {
		ret = platform_get_irq(pdev, i);
		if (ret < 0)
			return ret;
		npu->irq[i] = ret;
	}

	mem = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!mem) {
		dev_err(dev, "missing memory-region phandle\n");
		return -EINVAL;
	}
	ret = of_address_to_resource(mem, 0, &rsv);
	of_node_put(mem);
	if (ret)
		return ret;
	npu->rsv_phys = rsv.start;
	npu->rsv_size = resource_size(&rsv);
	npu->rsv_vaddr = devm_memremap(dev, rsv.start, npu->rsv_size, MEMREMAP_WB);
	if (IS_ERR(npu->rsv_vaddr))
		return PTR_ERR(npu->rsv_vaddr);

	npu->clk = devm_clk_get_optional_enabled(dev, "npu");
	if (IS_ERR(npu->clk))
		return PTR_ERR(npu->clk);

	npu->rst = devm_reset_control_get_optional(dev, "npu");
	if (IS_ERR(npu->rst))
		return PTR_ERR(npu->rst);

	/* Keep NPU in reset while we copy npu_data into its local SRAM. */
	ret = econet_npu_boot(npu);
	if (ret)
		return ret;

	dev_info(dev, "before set_pkt_buf_addr\n");
	ret = econet_npu_wifi_offload_set_pkt_buf_addr(npu);
	if (ret)
		return ret;

	/* Notify the NPU while it is still in reset: the A53 can write the
	 * sram mbox, then releasing reset lets the NPU read it. */
	dev_info(dev, "before notify_mbox\n");
	ret = econet_npu_notify_mbox(npu);
	if (ret)
		return ret;

	/* Release reset so the NPU can run. */
	if (npu->rst) {
		dev_info(dev, "releasing npu reset\n");
		ret = reset_control_deassert(npu->rst);
		if (ret)
			return ret;
		udelay(100);
	}

	platform_set_drvdata(pdev, npu);
	dev_info(dev, "EcoNet NPU init done\n");
	return 0;
}

static void econet_npu_remove(struct platform_device *pdev)
{
}

static const struct of_device_id econet_npu_of_match[] = {
	{ .compatible = "econet,ecnt-npu" },
	{ }
};
MODULE_DEVICE_TABLE(of, econet_npu_of_match);

static struct platform_driver econet_npu_driver = {
	.probe = econet_npu_probe,
	.remove = econet_npu_remove,
	.driver = {
		.name = "econet-npu",
		.of_match_table = econet_npu_of_match,
	},
};

module_platform_driver(econet_npu_driver);

MODULE_AUTHOR("Reverse-engineered port");
MODULE_DESCRIPTION("EcoNet EN7523 NPU init driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("econet/npu_rv32.bin");
MODULE_FIRMWARE("econet/npu_data.bin");
