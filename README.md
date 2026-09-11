# thunderbolt-ibverbs — fleet build

RDMA verbs over Thunderbolt/USB4 DMA rings, forked from
[hellas-ai/thunderbolt-ibverbs](https://github.com/hellas-ai/thunderbolt-ibverbs)
at upstream commit `d740562`. This repo is the exact source the Horde fleet
builds and runs on its zeus↔fedora link: Apple M2 Ultra (Asahi 7.1.13-usb4gpu,
Apple NHI) on one end, AMD Strix Halo (Fedora 6.18-rc7, stock PCI NHI) on the
other, over a 20 Gb/s Thunderbolt leg.

Every change after the upstream import is one lane patch, committed in the
order it was applied. `patches/` holds the raw diffs; the measured evidence
for each is in the lane spec `build-lanes/lanes/tbv-uc-scale-stall.md` of the
horde repo.

## Patch set (in apply order)

| patch | what it fixes | measured effect |
|---|---|---|
| `tbv-apple-nhi-throttle.patch` | programs the Apple NHI interrupt throttle (0xd004c) at ring activation; the register sat at its firmware default of 255 × 256 ns = 65.28 µs | 2 B latency 65.31 → **14.83 µs** (4.4×) |
| `tbv-native-credit.patch` | credit batch 32→256, native ring 1024→4096 | +28–51 % at 64–256 KB |
| `tbv-stock-kernel-compat.patch` | compile probes so one source builds on Asahi's extended `tb_ring`/`tb_nhi` and on stock kernels | fedora builds unmodified |
| `tbv-fedora-ring-diagnostics.patch` | read-only PCI NHI ABI diagnostic for 6.18 | debugging only |
| `tbv-uc-local-completion.patch` | the hybrid UC completion: verbs WC at local TX drain, wire retransmit budget (`TBV_UC_WIRE_MAX_RETRIES=7`), UC RNR retry budget, sendq slot release at drain, RX active-msg watchdog out-waits the wire budget, orphan fragments buffered not fatal | 1M-checked UC pingpong passes (was: permanent freeze every run) |
| `tbv-rx-supp-poll.patch` | enables the driver's own supplemental RX poll (native-only auto) that reaps frames whose completion interrupt was lost — the actual freeze mechanism | 18 334 rescues in one run; freeze eliminated |

## Measured acceptance (2026-09-11, both ends checksummed)

- **1M UC pingpong, 4096 B fragmented sends: PASS** — 96.19 s, 681.33 Mbit/s,
  96.19 µs/iter, rc=0 on both ends.
- **Gate rung 1, 64 B, 10 × 1M runs: converged** — floor **58.75 µs/iter**
  (block floors 59.55 / 58.75), all runs checksum-validated.
- Recovery accounting for the 1M run: 18 334 supp-poll rescues + 11 reorder
  recoveries, **0 retransmits, 0 RNRs, 0 errors** — every FA57 wire loss is
  absorbed in the fast paths.

Known gaps, stated rather than papered over: the FA57 TX hardware itself
still loses ~1 frame per few hundred (now recovered, never fatal), the
wire-retransmit stall costs ~5.09 s on the rare frame that is truly lost (a
selective fragment NAK would shrink it to ~200 µs), and 1 MB+ bandwidth wants
bigger native frames. Latency and bandwidth are the next round.

## Build

```bash
cd kernel
make -j$(nproc) modules          # against the running kernel's build dir
sudo cp thunderbolt_ibverbs.ko /lib/modules/$(uname -r)/updates/
sudo depmod -a
# module parameters (see scripts/fleet/tbv-configure-leg.sh in the horde repo):
#   profile, roce_netdev per leg; nhi_interrupt_throttle_ns=256
#   tx_progress_poll=1 rx_supp_poll=-1  native_tx_max_inflight=1
```

Never reload a module while either end of a Thunderbolt link has frames
posted to hardware that have not completed — the horde repo's
`scripts/fleet/rdma-peer-safe-to-disturb.sh` encodes that check.
