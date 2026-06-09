# Changelog

## [0.3.0](https://github.com/philippwaller/esphome-epaper-spectra6-133/compare/v0.2.0...v0.3.0) (2026-06-09)


### ⚠ BREAKING CHANGES

* **display:** make clear only fill the framebuffer ([#55](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/55))

### 🚀 Features

* Add optional update check package ([#54](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/54)) ([009d6ee](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/009d6ee23188edb79ea2381827242ac9191bd324))
* **display:** Expose component version via public API ([#52](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/52)) ([7bdb1f5](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/7bdb1f55b14624a618c55507eed1f3556e65edbe))


### 🐛 Bug Fixes

* **display:** Make clear only fill the framebuffer ([#55](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/55)) ([f9897b4](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/f9897b49808e34204a04c3adfde7a9a15499f4e8))

## [0.2.0](https://github.com/philippwaller/esphome-epaper-spectra6-133/compare/v0.1.2...v0.2.0) (2026-06-07)


### 🚀 Features

* **display:** Introduce deep sleep mode with automatic wake-up ([#48](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/48)) ([6d38f43](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/6d38f43f1cbbb4e20a074114bc5b2fd9554381c3))


### 🐛 Bug Fixes

* **display:** Keep ESPHome responsive during long draw operations ([#47](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/47)) ([652ae18](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/652ae18bd9351ebba51cd3392ffe24923a593c7b))


### ⚡ Performance

* **framebuffer:** Speed up changed-region detection ([#50](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/50)) ([5753f1c](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/5753f1c4a82390e5b5977641dc4f2dcb0d982528))
* Inline color_to_code function for optimized pixel mapping ([#39](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/39)) ([609a16c](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/609a16c904d491bce83da51f747dd4c2f73b8d20))
* Optimize image draw throughput ([#14](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/14)) ([ce602d2](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/ce602d217b080a956ee2135cbe7fb87f97ceda6a))
* Optimize pixel writing and add benchmarks for draw pipeline ([#36](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/36)) ([699f2f8](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/699f2f8a18dade906322a14e3120e8cc76808e13))
* Reduce draw_absolute_pixel_internal overhead in hot path ([#37](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/37)) ([1ddc1c0](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/1ddc1c0f36e3567b3bc099ab72d39e439f79b1ee))


### 🔧 Maintenance

* Add shared AGENTS.md instructions ([#38](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/38)) ([edca512](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/edca512649169e1ebbb11246f13685c94c04204e))
* **python:** Centralize runtime version ([#41](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/41)) ([08212ae](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/08212ae1c54e7c0af9f746d7b1cf8028b243fbde))


### 📖 Documentation

* **readme:** Add board package instructions ([#42](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/42)) ([5b16b69](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/5b16b69998b796b0aff056df041bff6c84e66f4e))
* **readme:** Fix formatting and link consistency ([#44](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/44)) ([532f368](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/532f368c0b4e1a605a5bfadc4e8d175076e83616))

## [0.1.2](https://github.com/philippwaller/esphome-epaper-spectra6-133/compare/v0.1.1...v0.1.2) (2026-06-04)


### 🔧 Maintenance

* **deps:** Pin dependencies ([#21](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/21)) ([661dd61](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/661dd61a66bfd6a6524ab108cddbbe4cef0d6113))
* **deps:** Update dependency gcovr to v8 ([#31](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/31)) ([935ca93](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/935ca9378e825975ded18cab8a727a34f7c0c529))
* **deps:** Update dependency packaging to &gt;=24.2 ([#28](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/28)) ([f10d470](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/f10d47084a6b9f9d6e1dfadff608736c6dcfebba))
* **deps:** Update dependency packaging to v26 ([#32](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/32)) ([05bc39a](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/05bc39a71630942c0c50113f34e8d1ed8c627e6c))
* **deps:** Update dependency pre-commit to &gt;=4.6.0,&lt;5 ([#29](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/29)) ([09944fc](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/09944fc0fb2256d2d6ff538ecd93637acbd54832))
* **deps:** Update dependency pytest to &gt;=9.0.3 [security] ([#22](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/22)) ([0d84841](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/0d8484175f615ba0bf9471abb130b6a2f92b31a7))
* **deps:** Update dependency pyyaml to &gt;=6.0.3 ([#27](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/27)) ([3e8032e](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/3e8032efaedc897eb8188977b54bab9ffc9c2ab8))
* **deps:** Update pre-commit-hooks to v0.15.16 ([#24](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/24)) ([6b9c5db](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/6b9c5dbdfad142b874de2072795de9222f796802))
* Disable esphome updates in Renovate configuration ([#18](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/18)) ([25ca33b](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/25ca33be5738a0807fe5bba0c9a8b4ab2e4595b8))
* Replace Dependabot with Renovate ([#17](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/17)) ([6211074](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/62110744a7dc50ffaaea710637ea0939c0281dbb))


### 📖 Documentation

* Improve quick start guide and clarify component import ([#11](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/11)) ([3c8d822](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/3c8d822432590a9204a7019fba41e9bce1dd4ce2))

## [0.1.1](https://github.com/philippwaller/esphome-epaper-spectra6-133/compare/v0.1.0...v0.1.1) (2026-05-31)


### 🐛 Bug Fixes

* **controller:** Harden low-level transfer guards and log invalid inputs ([#8](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/8)) ([2216152](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/2216152e066f69085953b6dc34613d4ddb67a938))
* Properly override clear() to fix black screen on auto-clear ([#10](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/10)) ([4b83961](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/4b83961912a40d4a81683b00564a7e94c044af02))


### 🔧 Maintenance

* **scripts:** Replace bootstrap-venv.sh with unified setup script ([#7](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/7)) ([9b10319](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/9b1031952f7deb50451bd35b708dfaec0c03cc93))
* Update pre-commit hooks to latest versions ([#3](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/3)) ([994a786](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/994a7863ae3e03189aa69fd56542210e6719a6fe))


### 📖 Documentation

* **readme:** Add release-please markers ([#6](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/6)) ([2dcee7f](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/2dcee7f8b750dbbd504fd9e59d13aa0c62782975))
* Use stable release ref in YAML examples ([#5](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues/5)) ([b31c146](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/b31c146f416855432628ea7d69fef401135728d5))

## [0.1.0](https://github.com/philippwaller/esphome-epaper-spectra6-133/compare/v0.0.1...v0.1.0) (2026-05-30)


### 🚀 Features

* add ESPHome component for 13.3″ Spectra 6 e-paper displays ([cc9efac](https://github.com/philippwaller/esphome-epaper-spectra6-133/commit/cc9efacf0269c2106d292a9d826ce2e3abb14e07))
