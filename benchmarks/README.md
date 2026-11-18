# EDA Elite Challenge benchmarks

Tables I and II of UniMatcher use the three layouts below from each competition. Each layout is stored as an XZ-compressed contest TXT file. The pattern files, reference outputs and Python judges can be read directly from a checkout.

## Unpack and verify

Run the following commands from this directory on Linux. `sha256sum` checks the files as distributed. `xz -dk` creates the layout text alongside each archive and keeps the compressed copy. The six decoded layouts occupy about 1.01 GiB in total.

```sh
sha256sum -c SHA256SUMS
find eda2023 eda2024 -type f -name '*.txt.xz' -exec xz -dk -- {} +
sha256sum -c LAYOUTS.sha256
```

To unpack only the 2023 Small layout, run `xz -dk eda2023/public_case/layout/small.txt.xz`. Both manifests use paths relative to this directory. The patterns and answers retain their original text, including line endings and flexible-edge annotations.

## 2023 · Single-layer pattern matching

Paths below are relative to `eda2023/`. Small and Large share a pattern library. Hidden has its own library. Each layout is evaluated separately with `lib1.txt`, `lib2.txt` and `lib3.txt`. `lib3` includes the contest's bracketed fuzzy constraints.

| Case | Decoded layout | Pattern directory | Reference directory |
| --- | --- | --- | --- |
| Small | `public_case/layout/small.txt` | `public_case/lib/` | `public_case/answer/small/` |
| Large | `public_case/layout/large.txt` | `public_case/lib/` | `public_case/answer/large/` |
| Hidden | `hidden_case/layout/layout_fin.txt` | `hidden_case/lib/` | `hidden_case/answer/` |

Each reference directory contains two marker lists per library. Select `libN_exact.txt` for the original orientation or `libN_wi_rot_mir.txt` when rotations and reflections are enabled. To run 2023 Small `lib1` with the default eight orientations after building the CLI and unpacking the layout, create a new output directory and compare its first marker list with the reference.

```sh
workdir=$(mktemp -d)
../build/unimatcher-contest 2023 eda2023/public_case/layout/small.txt \
  "$workdir/out" eda2023/public_case/lib/lib1.txt
python3 eda2023/judge/pmJudge.py -res "$workdir/out/pattern_0/marker.txt" \
  -ans "$PWD/eda2023/public_case/answer/small/lib1_wi_rot_mir.txt"
```

The 2023 judge prints the numbers of extra and missing markers. Both counts should be zero.

The fifth Integrated Circuit EDA Design Elite Challenge published Task 10, *Ultra-large-scale Layout Pattern Matching*, with Semitronix as the problem provider. The contest page lists the CASE download. The local 2023 materials contain no separate license notice. Permission for external redistribution is unverified. [Official contest materials, Task 10](https://cpipc.acge.org.cn/cw/contestPrevious/detail/2c9080158ee9c272018f229208b610a6/2c9080158f815e21018fba6202d92461?page=1)

## 2024 · Multilayer design-unit verification

Paths below are relative to `eda2024/`. Each case has a layout, one pattern file containing all its templates, and a reference output. The reference records markers, erroneous layers and geometric XOR contours. Each reported marker has at least three matching layers under one allowed orientation and translation, with differences on at least one layer. A marker that matches every layer in any allowed orientation is excluded.

| Case | Directory | Decoded layout | Patterns | Reference | Templates |
| --- | --- | --- | --- | --- | --- |
| Small | `public/small/` | `small_case.txt` | `small_pattern.txt` | `small_answer.txt` | 2 |
| Large | `public/large/` | `large_layout.txt` | `large_pattern.txt` | `large_answer.txt` | 3 |
| Hidden | `Hidden Case/` | `hidden_layout.txt` | `hidden_pattern.txt` | `hidden_answer.txt` | 3 |

To verify all templates for 2024 Small, run the CLI and compare its result file with the supplied reference.

```sh
workdir=$(mktemp -d)
../build/unimatcher-contest 2024 eda2024/public/small/small_case.txt \
  "$workdir/result.txt" eda2024/public/small/small_pattern.txt
python3 eda2024/judge/pmJudge.py -res "$workdir/result.txt" \
  -ans "$PWD/eda2024/public/small/small_answer.txt"
```

A successful 2024 comparison prints `PASSED.`. Both judges report their verdict on standard output.

Credit the **2024 China Postgraduate IC Innovation Competition · EDA Elite Challenge Contest** and **杭州广立微电子股份有限公司 (Semitronix)**. Their dataset v1.0 notice, dated 2025-03-06, names CC BY 4.0 and lists additional conditions in section 3.2. The full notice is preserved in its original language. [Dataset notice](eda2024/DATA-NOTICE.txt) · [Organizer’s 2024 download guide](https://cpipc.acge.org.cn/cw/contestPrevious/detail/2c9080158ee9c272018f229208b610a6/2c908018961528a0019618b96506022a?page=0) · [Official problem specification](https://edaoss.icisc.cn/file/cacheFile/2024/8/7/3086e8dc69e441cbb47cddf961caae7e.pdf)
