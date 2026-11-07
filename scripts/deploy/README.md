# Live deployment procedure

This procedure is for the actual DAQ host shell as user `daq`. It changes the
live checkout, services, and selected ODB configuration. Do not run it from the
development sandbox. Finish development and tests under `/home/nagao`, commit
and push the intended changes, then schedule a maintenance window with the run
STOPPED and no Run Start during deployment.

Runlog `EventSlipCount` uses `INT64` so `-1` represents N/A. The whole-deployment
preflight rejects an existing different type before writing ODB. The legacy
conversion helper remains covered by unit tests, but normal deployment does
not silently convert an existing `UINT64` key.

From the live checkout, the normal update is:

```sh
cd /home/daq/midas/midas/online
./scripts/deploy/deploy1_init.sh
```

Check its completion message and the read-only step 4 smoke test before
resuming operation. The script requires user `daq`, host `nexdaq1`, branch
`main`, a clean tracked working tree, and experiment `daq` STOPPED with no
transition on mhttpd port 8081. It also requires the existing MIDAS, ROOT,
ROOTANA, Runlog directory, `exptab`, and four installed systemd units. The
installed monitor and analyzer units must match their tracked files. The `daq`
user needs passwordless permission to stop/start only those two services.
Deployment does not install or update units.

If this is the first deployment of these scripts and the live checkout lacks
them, fetch the committed bootstrap once from the actual host shell with
`git pull --ff-only origin main` after checking for tracked local changes.
Later updates use `deploy1_init.sh`.

## One-time analyzer unit migration

If an older installed analyzer unit still points to
`analyzer/build/midas_analyzer`, the unit comparison in step 2 stops before the
build. During a STOPPED maintenance window, prepare the new executable and
install the tracked unit before running step 1:

```sh
cd /home/daq/midas/midas/online
export MIDASSYS=/home/daq/midas/midas_src ROOTSYS=/home/daq/root/current
cmake -S analyzer -B analyzer/build \
  -DROOTANA_DIR=/home/daq/midas/rootana \
  -DROOT_DIR=/home/daq/root/current/cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build analyzer/build -j2
test -x analyzer/bin/midas_analyzer
sudo install -m 0644 systemd/midas-analyzer.service \
  /etc/systemd/system/midas-analyzer.service
sudo systemctl daemon-reload
./scripts/deploy/deploy1_init.sh
```

Installing the unit and reloading systemd do not restart a running analyzer.
Step 1 builds and tests again, then restores only processes that were running
when deployment began. Do not remove the recovery journal or revert the unit
as a way around a failed unit comparison.

## Steps and effects

- `deploy1_init.sh` checks identity, checkout, run state, and initial
  frontend/service state. It holds an exclusive lock, copies its bootstrap,
  fetches `origin/main`, displays incoming files, merges with `--ff-only`, then
  calls step 2. It does not clean
  untracked files; Git stops if one conflicts with a tracked incoming path.
- `deploy2_impl.sh` backs up ODB to a unique file and writes a recovery
  journal. It stops only the frontends and monitor/analyzer services that were
  running when needed, builds VME/EASIROC/HUL/monitor/analyzer and runs their
  tests, calls step 3, prepares
  missing derived Runlog files, restores the original process state, and calls
  step 4. A completed deployment of the same commit and binaries does not
  restart processes again.
- `deploy3_configure_live_odb.py` validates DAQ settings schema before changing
  its owned fixed schema, missing EASIROC Settings, Runlog BOR/EOR links and
  order, Programs, and Custom entries. It does not load an entire ODB backup. Unexpected schema, links,
  types, or paths stop deployment.
- `deploy4_check_live.py` checks the Git commit, binaries, fixed ODB schema,
  Programs, Custom pages, Runlog/ELOG resources, service/frontend state, and
  port 8081 without writing ODB or starting a run. Its checks read live state,
  so run it only from the actual host shell.

`scripts/archive_midas_data.sh` is a **tracked** script in this repository.
It is separate from these four deployment steps; executing it copies data to
the mounted archive and is not a smoke test. Deployment does not run it or
delete untracked files. Neither `git reset --hard` nor `git clean` is used.

## Failure and recovery

Keep Run Start disabled operationally while resolving a failure. ODB backups
are written below `/home/daq/midas/midas/backups/` as
`odb-before-deploy-*.odb`; the recovery journal is
`/home/daq/midas/midas/deploy-state.json`. Correct the reported cause and
rerun `deploy1_init.sh`. The journal retains the original process state; do
not delete it manually. No automatic full-ODB restore is performed.

The scripts recheck STOPPED before change stages, but they cannot atomically
block every MIDAS Run Start path. A detected race stops deployment.

Existing Run Number, Run Parameters, ELOG history, hardware settings, Analyzer
Histogram/Page settings, DAQ data, Runlogs, ELOG files, `runlog_selection.json`,
and unrelated Programs/Custom entries are preserved. Missing history keys may
be initialized to zero or empty values. `/Custom/Path` and the ELOG web port
are verified against the intended live values rather than overwritten.

For targeted diagnosis on the actual host, check STOPPED first:

```sh
cd /home/daq/midas/midas/online
python3 scripts/deploy/deploy4_check_live.py        # read-only smoke check
python3 scripts/deploy/deploy3_configure_live_odb.py # changes owned ODB keys
```

Do not run step 2 alone; it needs the bootstrap lock and initial process state.
Static syntax checks, which do not connect to ODB, are:

```sh
bash -n scripts/deploy/deploy1_init.sh scripts/deploy/deploy2_impl.sh
python3 -m py_compile scripts/deploy/deploy3_configure_live_odb.py scripts/deploy/deploy4_check_live.py
```

## HUL ProgramsのFirst failed欠損によるSIGABRT（2026-11-05）

本番デプロイ時、`/Programs/fehul/First failed`が存在せず、
`daq_monitor`と`odbedit`がSIGABRTで終了する障害が発生した（障害情報は作業依頼による）。
開発ソースではdeploy3がHULのProgramsを部分的に作成し、通常はMIDASの
クライアント登録が用意する`First failed`を作成していなかった。
全体デプロイは新規HULを起動しないため、その登録による補完も行われない。

参照MIDAS `/home/nagao/midas/midas_src/src/midas.cxx`の
`cm_set_client_info()`は`First failed`を`(uint32_t)0`で定義する。
`src/alarm.cxx`の`al_check()`はProgramsの各ディレクトリについて、
`Required=false`でも`First failed`を読み、停止検出時刻を記録し、
稼働検出時などに0へ戻す。欠損した子キーの参照は`src/odbxx.cxx`で例外となる。
HULが任意参加・未起動でも、この監視用キーを省略できない。

開発版deploy3のfevme・feeasiroc・fehul共通schemaへ
`First failed`（UINT32 / MIDAS DWORD、1要素、初期値0）を追加した。
既存値は監視履歴なので非ゼロを含め保持し、`MANAGED_VALUES`には含めない。
型・要素数不一致はpreflightでODB書込み前に拒否し、自動変換・削除はしない。
同じschemaを使うdeploy4でも欠損・型・要素数を検出する。
3 frontendの定義はMIDAS標準9項目（Required、Watchdog timeout、Check interval、
Start command、Auto start、Auto stop、Auto restart、Alarm class、First failed）を満たす。
`scripts/frontend_programs.odb`の既存VME/EASIROC定義も`First failed = DWORD : 0`で整合する。
他のProgramsの設定と全体デプロイの順序・起動復元規則は変更しない。

復旧は別途承認された本番保守作業として、実DAQホストの通常シェルで行う。
RUNをSTOPPED・遷移なしに保ち、既存のバックアップと復旧journalを保持する。
mhttpd 8081のODBエディタ等で対象実験を確認し、`/Programs/fehul`の9項目を検査する。
`First failed`が欠損する場合だけDWORD・1要素・0を作成する。
既存の正常値を0へ戻したり、型・要素数不一致のキーを削除して作り直したりしない。
不一致は現状を記録し、個別の修復方針を決めてから再開する。
修正版を使い、上記Failure and recoveryに従って`deploy1_init.sh`を再実行し、
deploy4と元のサービス状態の復旧を確認する。キー補完目的のfehul起動やODB全体のloadは不要。

今回の作業は開発ソースとメモリ内ODBモックだけで検証する。
空のProgramsツリーの新規作成、0/非ゼロ/UINT32最大値の保持、
型・要素数不一致の書込み前拒否、他Programs不変、previewとsmokeの検出、
全体デプロイの回帰を対象とする。本番8081・`/home/daq`、開発ODB、
サービス・実機は操作せず、実ODBでの復旧・SIGABRT解消は未検証。

## ODB settings scope and comparison

Steps 3 and 4 validate the existence, type, scalar/array length, and required
STRING capacity of VME/EASIROC Settings and existing Analyzer settings. A
schema error stops deployment with the affected path and expected/actual
schema. Deployment does not copy development ODB setting values, repair schema,
or change existing equipment setting values. Missing EASIROC Settings are added
from source defaults as described below; HV/HUL Settings stay frontend-owned.
Frontends and the analyzer validate
the range, meaning, and combinations of values when they use them.

Existing custom Analyzer Histogram groups and Pages are allowed; deployment
does not require the default layout. An absent Analyzer tree retains the
analyzer's in-memory fallback, and absent Pages retain default pages. Missing
Pad fields remain empty pads. The deploy-owned `Programs`, Runlog JSON, and
Custom values listed in `MANAGED_VALUES` retain their existing policy. Compare
development and production setting values with the read-only
[`odb_settings_diff.py`](../README-odb-diff.md) from the actual DAQ host shell.


## EASIROC・HUL対応の現行実装（2026-11-05、開発側のみ・本番未反映）

本節が変更後の仕様である。後続の調査各節は変更前の履歴として残す。
過去の「EASIROC journalが全体反映を拒否」「HULは対象外」「不足Settingsは生成しない」
という記述は本節で更新される。今回行ったのは開発リポジトリの修正、ビルド、隔離モックのみ。
本番8081へのRPC、ODB変更、frontend起動、デプロイ実行、実機通信は行っていない。
ビルド依存として本番MIDASのヘッダ・ライブラリ・CMake定義を読み取ったが、
`/home/daq`への書込みや本番プロセス操作はない。

### 入口・配置・サービス

入口と実行順序はdeploy1 → deploy2 → deploy3 → deploy4のまま。
deploy1のEASIROC専用journal探索・拒否を削除した。
`deploy_easiroc.py`と配布物作成処理は維持し、全体反映から呼び出さない。
共通化はMIDASリンク依存を読む`midas_build.py`だけであり、専用反映の完了記録・
バックアップ・配布物を全体反映の条件にしない。
Git clean検査は維持し、trackedの作業ツリー/index差分があれば理由・ファイル名・
差分統計を表示して停止する。reset/stash/cleanは行わない。
通常反映はfetch後のcommit間の対象ファイルを表示してから`merge --ff-only`を行う。
従来のfast-forward限定方針と実行方法を維持する。

将来の実行時は実際のDAQホストの`daq` shellで使う。今回これらは実行していない。

```sh
cd /home/daq/midas/midas/online
./scripts/deploy/deploy1_init.sh --check  # 読取り専用の予定確認
./scripts/deploy/deploy1_init.sh          # 従来の全体反映（別途実施時のみ）
```

`--check`はbootstrapコピー・lock作成・fetch・build・ODB書込み・process操作を行わない。
RPCは既存mhttpdへの`db_get_values`/`db_key`/`db_copy`/`cm_exist`に限定し、
変更メソッドは送信前に拒否する。ローカルMIDAS clientは起動しない。
Git差分一覧、現在の対象ファイルのSHA256、frontend状態、ODB追加予定/保持値/
frontend生成待ちキー、従来の固定管理値変更予定、対象サービスを表示する。
`origin/main`はローカルcacheの比較であり、最新remoteやビルド成功の保証ではない。
通常反映でもstep2のbackup・停止・ODB変更より前にschema検査と予定表示を行う。
GUIはcheckout更新時点でserved fileが変わるため、incoming file一覧はGit更新前に表示する。

| 対象 | ビルド・配置・登録 | 起動方針 |
| --- | --- | --- |
| EASIROC | `make -C easiroc_frontend feeasiroc`、`easiroc_frontend/bin/feeasiroc`、既存`start_feeasiroc.sh` | 元から稼働中の場合だけ復帰 |
| Slow Control GUI | Gitで`daq_monitor/web/easiroc.{html,js,css}`と`easiroc-mock-data.js`を配置、`/Custom/EASI Slow Control=easiroc.html`を不足時だけ登録 | `/Custom/Path`は既存のlive webディレクトリであることを検査 |
| HUL | `make -C hul_frontend all`、`hul_frontend/bin/fehul`、新規`scripts/start_fehul.sh`、`/Programs/fehul` | 新規HULは起動せず、元から稼働中のHULだけ復帰 |
| VME・monitor・analyzer | 従来のビルド・テスト・サービス・起動方法 | 維持。新規systemd unit導入なし |

追加リンク依存はインストール済みMIDASの`lib/midas-targets.cmake`から取得する。
MIDAS自体はビルド・変更しない。GUI試験用のNodeとMIDAS依存定義を停止前に確認する。
新規HULのProgramsはRequired=false、Auto start/stop/restart=false。
既存値は保持するが、既存EASIROC/HUL起動commandや自動起動flagがこの運用と異なれば
上書きせず事前検査で停止する。

### ODBの生成責任と保護

実装は`frontend_odb.py:plan/validate/apply`とdeploy3の`preflight/configure`。
開発ODBはコピーしない。既存Settings・本番固有キー・IP・ASIC値・LastApplied・Commandsを保持。
対象の既存キー/親directoryの型、要素数、必要なSTRING容量を最初に検査する。
不整合は停止・backup・ODB変更前に拒否し、自動変換・再生成しない。
追加直前の計画再比較と追加後のschema/値readbackを行う。
EASIROC/HULのIP重複、HUL Event ID 10/11の他Equipmentとの重複も拒否する。

| 対象 | 不足時の生成責任・初期値 | 既存値 |
| --- | --- | --- |
| EASIROC基本Settings 22項目 | deploy3。現行ソース定義のIP `.40`、ApplyAtBOR=false、InputDAC各32要素480等 | 保持 |
| EASIROC拡張Settings 11項目 | deploy3。下表の値で不足分のみ追加 | 保持 |
| HV Settings 2項目 | frontendの`process_hv_requests()`。CurrentLimit_uA=20 (DOUBLE)、HardwareAccessEnabled=false (BOOL) | 保持。既存trueも勝手にfalseへ変更しない |
| HUL Settings 6項目 | frontendの`frontend_init()`→`load_settings(...,true)`。IPAddress=`192.168.10.60`、RBCPPort=4660、TCPPort=24、EnableBlock=3、FreshnessMs=2000、DrainMs=2000 | 保持。deploy3で重複生成しない |
| HUL Common/Enabled | 不足時だけdeploy3がBOOL=falseで追加。後のMFE Common record生成は既存値を保持 | 既存true/falseを保持 |
| HUL Variables | frontendの`ensure_variable()`が不足DWORD配列HCUM[64]、HDIF[64]、HVAL[15]を0で生成 | 起動時は既存配列を保持。運転時に測定結果で更新 |
| HUL Status | frontendがConnection/GateをUnknownで公開、loopでConnection/CommCheckedAt、異常時にLastErrorを更新 | 起動・運転で更新される状態表示。deploy3は書き込まない |
| EASIROC Commands/Variables/LastApplied | frontendがmailboxを管理。deploy3は既存schema検査だけ | 履歴・要求IDのコピー/再初期化なし。有効なLastAppliedが不完全なら拒否 |

拡張11項目は`easiroc_run_settings.h:visitSlowControlSettings()`に定義され、
`easiroc_odb.cxx:readSettingsExtras()/transferExtras()`が読み、ASIC Apply時のsnapshot/符号化で使用する。
frontendは拡張Settingsを自動補完せず、一部だけあると読出しに失敗するため、
deploy3がfrontend再開前に不足分を揃える。

| Settings相対パス | 初期値・型 |
| --- | --- |
| Probe/ASIC1/Enable、Probe/ASIC2/Enable | false、BOOL（2項目） |
| Probe/ASIC1/Channel、Probe/ASIC2/Channel | 0、INT（2項目） |
| Probe/ASIC1/Signal、Probe/ASIC2/Signal | 4、INT（2項目） |
| SelectableLogic/Pattern | 3、INT |
| SelectableLogic/OneChannel | 0、INT |
| SelectableLogic/HitNumThreshold | 1、INT |
| SelectableLogic/AndChannels | false×64、BOOL配列 |
| TriggerWidth | 118 ns、INT |

順序は「不足を許容する事前schema検査 → backup → 元の稼働client停止 → build/test →
deploy3で不足EASIROC Settings/HUL無効Common生成 → 元のprocess状態復帰 → 読取り専用smoke」。
deploy3担当キーは事後検査で欠損を拒否する。
HV2項目/HUL6項目は未起動時の欠損を許容し、存在する場合だけ型・値を検査する。
不足キー生成時はEASIROC/HUL停止を確認する。ODB生成のための事前frontend起動・実機通信・HV操作は不要。
`CurrentLimit_uA=20`、`InputDAC=480`は希望値として記録し、既存本番値は変更しない。
不足キーの初期値とは区別し、既存値への適用は後日のODB差分確認後に個別判断する。
従来のVME Programs/Runlog等の明示的固定管理値（`MANAGED_VALUES`）の収束は維持し、
変更予定を事前表示する。全ODB値を無条件に保持する機能ではない。
feeasiroc Programsは固定上書き対象から外し、既存値保持と不整合時の拒否に変えた。

### 実機通信なしのHUL導入確認と限界

新規HULを起動せず、ビルド・バイナリ・wrapper・Programs・無効Common・GUI・
既存サービス状態を検査できる。6 Settings/Variablesの実生成は後日の明示的な初回起動時。
配備成功を実データ取得成功と扱わない。
現行`fehul.cxx`のinit自体はODB処理だが、`frontend_loop()`はCommon/Enabled=trueなら
STOPPEDでもRBCP到達性確認を開始し得るため、新規HULは無効Commonを先に用意し自動起動しない。
既存HULのEnabled=true、EASIROCのHardwareAccessEnabled=trueは保持するので、
将来の通常反映で元の稼働状態へ復帰すれば監視通信が再開し得る。
保守時の実機競合・HV状態の確認は別途必要。

HUL BORはEnabledをlock/readし、falseなら実機操作なしで終了する。
trueならgate OFF確認、TCP接続/drain、reset・設定readback、gate ONを行う。
EORはgate OFF、受信drainと最終イベント（ID11）、受信thread終了を処理する。
通常イベントID10。TCP切断等のfaultはStatus/最終recordに反映され、無条件再接続で欠損を隠さない。
TriggerSelectはBOR側の固定設定であり、ODB Settings 6項目には含まない。
これらはソース・モック確認であり実機データ取得の認証ではない。
以前のRUN186で完全frameは0であり、HUL実データ取得は未検証のまま。

### 途中失敗と復旧

従来のODB全体backup、commit表示、`deploy-state.json`と元の稼働状態だけを復帰する方針を維持。
旧journalにHUL状態がなく現在HULが稼働中なら、元の状態を推測せず拒否する。
完了済みの同commit/同binary再実行で不足キーもなければprocessを再起動しない。
追加キーは作成前intentと値設定/readback後writtenを既存journalの
`frontend_initialization`にatomic保存する。
作成後・値設定前の失敗や応答喪失など未完了intentがあると、再実行は黙認せず停止する。
表示されたパス・予定値・saved ODB backup・現値/型を実host shellで確認し、
明示的な修復判断をしてから再開する。journal削除や全ODB無条件loadで回避しない。
build失敗などODB追加前の失敗は、従来どおり原因を直してdeploy1から再開する。
自動の全ODB/旧バイナリ/旧GUI復元は追加していない。
版を戻す場合も従来のcommit/build復旧とODB backupで差分を確認し、
運転状態・Commands・LastAppliedを丸ごと復元しない。

### 開発側の検証結果と残事項

- Python全88件成功（全体反映用の新規19件を含む）。空のfrontend tree、部分欠損、
  既存値/本番固有値/LastApplied保持、全対象型/配列不一致、親型不一致、IP/Event ID衝突、
  作成/値設定/ビルド途中失敗、再実行、journal再開/未完了intent拒否を隔離モックで確認。
- 実際のdeploy2関数を抽出したモックでVME/monitor/analyzerのbuild/test呼出しと停止/復帰、
  既存HUL復帰、新規HUL非起動を確認。deploy4のGUI実bytes比較、deploy1のGit dirty拒否/
  `--check`分岐、readonly RPCの変更メソッド拒否も確認。
- EASIROC/HULの既存frontendバイナリを開発側の別build/binディレクトリにビルド成功。
  `/home/daq`のMIDAS依存を読取り専用で使用し、コンパイラ警告・エラーなし。
  EASIROC `make ... feeasiroc check check-web`（GUI164 checks）、
  HUL `make ... all check`（stream/snapshot/RBCP/load/run-guard/bank/monitorの7試験）成功。
  通信を伴う試験はloopbackのみで実機接続なし。
- C++の実定義から値を出力するデータ構造だけの試験でEASIROC追加定義/HUL6初期値との一致を確認。
  frontend/MIDAS clientは実行していない。既存専用EASIROC試験も成功。
- `bash -n`、Python構文、`git diff --check`を確認。Python suite末尾のstdout `ERROR` は
  既存ELOG異常系fixtureの出力で、unittest結果は `OK`。

未実施は本番反映/smoke、初回HUL起動、実ODBのMFE record生成、実機通信・HV・ASIC Apply・
DAQ運転、HUL実データ取得。VME/monitor/analyzerの今回の回帰確認はデプロイ経路のモックであり、
実プロセス統合運転ではない。
将来の反映前にはreview/commit/pushと本番Git clean、STOPPED/遷移なし、Node/MIDAS/ROOT依存、
既存起動command/Custom/Path、IP/ID、backup/復旧手順、開発との通信競合・HV状態を実hostで確認する。
本番固有値の不整合を推測で補正せず、予定差分を個別確認する。

## EASIROC・HUL本番反映の事前調査（2026-11-05 JST、以下は変更前の履歴）

本節は調査結果と将来の反映計画であり、実行手順の実施記録ではない。
本番8081・`/home/daq`は読取りのみ。既存mhttpdへの`db_get_values`と静的HTTP GET、
ファイル・Git・systemd情報の読取りを使用した。ローカルMIDAS client、反映スクリプト、
frontendは起動していない。実機通信、HV操作、RUN遷移、ODB/サービス変更は行っていない。
許可された開発側の文書と最終報告ログだけを更新した。

### 判定

| 対象 | 判定 | 根拠・導入条件 |
| --- | --- | --- |
| EASIROC frontend・HV・Slow Control GUI | **B：スクリプト修正後に反映可能** | frontendのビルド対象と起動経路は既存対応。しかしGUI登録、HV/拡張schema検査、HVを抑止した初回導入、対象別の復旧が不足。`deploy2_impl.sh:22,205,225`、`deploy3_configure_live_odb.py:133,289,369`、`deploy4_check_live.py:146`。現スクリプトをそのまま実行するA判定ではない。HVの実機運用開始は別の受入判定とする。 |
| HUL Scaler | **C：追加開発・検証が必要** | 新frontendのビルド・登録・状態復元が対象外。さらに実RUNの完全TCP frame受信0件でデータ取得未達。`deploy2_impl.sh:22,28,205`、`hul_frontend/IMPLEMENTATION_PLAN.md:163,171,178`、`hul_frontend/fehul.cxx:257,427,575`。スクリプト対応だけでは本番運用不可。 |

Bは反映方式を整備できるという判定であり、今回の導入承認やHV実機保護の保証ではない。
最新のHV表示修正はモックと開発での再起動後観測まで。実機昇降圧GUI・保護の受入確認が
不足する間はHV操作を本番で開放しない。受入失敗時は追加開発・検証に戻す。
HULを待たずEASIROCだけ導入・復旧できる構成を選ぶ。

### 読取りで確認した環境

2026-11-05 01:18〜01:23 JST頃の観測。動作中の状態は変わり得るため、反映時に再取得する。

| 項目 | 開発 `/home/nagao`・8181 | 本番 `/home/daq`・8081 |
| --- | --- | --- |
| Experiment | `daq-dev` | `daq` |
| Runinfo | RUN186、State=1 STOPPED、Transition=0、Requested transition=0 | RUN177、State=1 STOPPED、Transition=0、Requested transition=0 |
| client一覧 | mhttpd602942、Logger602946、ana602951、fevme626813、daq_monitor628754、feeasiroc1085730 | mhttpd298961、Logger298962、daq_monitor749417、ana749425 |
| frontend | feeasiroc・fevme接続、fehul不在 | fevme・feeasiroc・fehulすべて不在。Equipmentの残存は稼働証拠にしない |
| `/Custom/Path` | `/home/nagao/midas/midas/online/daq_monitor/web` | `/home/daq/midas/midas/online/daq_monitor/web` |
| Custom登録 | DAQ、Run Summary、EASI Slow Control=`easiroc.html` | DAQ、Run Summaryのみ。EASI Slow Controlの登録URLは404 |
| checkout HEAD | `8bbb1d19928f2c6a8b39d4c89d73f4da45517001` | `4d9f2f43bdb042de9a9ca8b75977879611d18849` |
| Git状態 | 調査開始時tracked変更なし | 調査時tracked変更なし |

両portの `/easiroc.html`、`.js`、`.css`、`easiroc-mock-data.js`、`daq.html`、`daq.js`
をcache回避GETし、HTTP200かつ各環境のディスクとバイト一致を確認。
登録済みDAQページは両port、登録済みEASIページは8181でHTML一致。
8081は旧EASIファイル自体を直接取得できるが、メニュー用Custom登録はない。
最初に試した `/CS/...` は両portで400だったため、実際の配信経路 `/ファイル名` と
`/?cmd=custom&page=...` により検証した。
開発JS SHA256=`9c834d1d16c68acf4fafc8e64e67a838362cda9e692eafed8bd019d3122c95cc`、
本番JS=`1bd0b7a4ab21303caff3bde17a74ba72eda16e4e8acf54d0d126a4710d48488f`。
HTML/CSS/mock-data/DAQ HTML/JSも開発と本番で異なる。ブラウザ実操作は行っていない。

本番4サービスはsystemctl showでactive/running：midas-mhttpd、midas-mlogger、
midas-daq-monitor、midas-analyzer。installed unitのExecStartはそれぞれ本番MIDASの
mhttpd/mlogger、本番`daq_monitor/bin/daq_monitor -e daq`、
`analyzer/bin/midas_analyzer --no-profiler`。exptabは
`daq /home/daq/midas/midas/daq daq`。frontend用の専用unitは確認できず、
feeasirocは `/Programs/feeasiroc/Start command` のwrapperで起動する構成。
fehulのPrograms・本番`hul_frontend/`・バイナリはいずれも存在しない。

### 既存反映スクリプトの範囲・副作用

- 手順は本READMEと`deploy1_init.sh`〜`deploy4_check_live.py`。
  開発ファイルをscpする方式ではなく、本番checkoutを`git pull --ff-only origin main`
  して本番内でビルドする（`deploy1_init.sh:4,8,37,96`）。従ってmainの全変更が入り、
  HULソース/共用DAQ GUIや無関係な変更もpull対象となる。component別反映はない。
  step1/2/4は開発・本番で同一SHA256、step3は異なる。pull後のstep3を基準に再レビューが必要。
- `deploy2_impl.sh:22-29,205-222` の対象はVME、EASIROC、monitor、analyzer。
  makeで各`bin/`、analyzerはcmake/buildから`analyzer/bin/`へ出力。
  MIDASSYS/ROOTSYS/ROOTANAは本番パスを指定。HUL用build/checkはない。
  Webはcheckout直下を配信するのでpull直後に更新され得る。binary切替と原子的ではない。
- step1はdaq/nexdaq1/main/clean tracked/8081 experiment daq STOPPED/遷移0と
  サービス状態を検査。bootstrap、lockを作り、元のfrontend・サービス稼働状態を記録する。
  step2は元々動いていたmonitor/analyzerとfevme/feeasirocを停止してbuildし、元の状態に戻す
  （`deploy2_impl.sh:175-243`）。mhttpd/mloggerは停止対象外。
  現本番のfrontend不在状態なら、更新後もfeeasirocを新規起動しない。
- 4サービスの存在・動作、monitor/analyzerのinstalled unitとtracked unitの一致等が前提。
  unitをinstall/updateする機能はない（`deploy2_impl.sh:167`）。他DAQへの影響として
  monitor/analyzerの一時停止、VME稼働時の停止・復帰、Runlog/Programs/Custom変更がある。
- ODB全体をloadせず、固定schema、Runlogリンク/順序、Programs、Customの管理値だけを更新。
  現行開発step3のSettings検査はVMEと旧EASIROC ASIC項目に限定される
  （`deploy3_configure_live_odb.py:339-381,475-505`）。HV、Devices、拡張Slow Control、
  LastApplied完全性、HULは検査対象外。検査に通ってもそれらの互換性は保証しない。
  Settingsの既存値を開発値で置換せず、欠損Settingsを埋める機能もない。
  `/Custom/EASI Slow Control`と`/Programs/fehul`も未管理。
- ODB backupは本番runtime `backups/odb-before-deploy-*.odb`。
  `deploy-state.json`に元の稼働状態・commit・完了時binary hashを記録する
  （`deploy2_impl.sh:128,256-304`）。backup時に実ホストでodbeditが起動されるので、
  今回は実行していない。旧binary/GUI/unit/依存ライブラリの退避機能はない。
  失敗時は停止状態のまま原因解消・同じjournalで再実行する方式で、
  自動的な旧版復元や全ODB restoreはない。Run Startの全経路を原子的には禁止できない。
- **dry-run機能はない**。`--dry-run`や`--help`を付けても安全な調査モードにならない。
  step1は専用`--stable`以外の引数をdry-runとして解釈せず、step2も渡された引数で
  副作用を抑止しない。step3は引数に関係なくconfigureへ進む。
  同commitの再実行もODB backup、step3、Runlog整備を実施する。
  step4だけは読取り検査だが今回それ自体も起動せず、必要なHTTP/ファイル照会だけ実施した。

### EASIROCの互換性とHV条件

- 正規binaryは`easiroc_frontend/bin/feeasiroc`。
  開発SHA256=`d12faf142d9c5098354f163b270887a1f9c4a16b4aaf2f99f041a1c822ccccd7`、
  本番=`1e323f04d1d970769200c05f1f1778860362c7b4f347d466df322f5cb0dfb75a`。
  本番には旧`easiroc_frontend/feeasiroc`も残るがwrapperはそれを使わない。
  wrapperはHOME基準のMIDASSYS/exptab/experimentを設定、remote系変数をunset。
  開発版は追加引数を渡せる（`scripts/start_feeasiroc.sh:4-23`）。専用SCプロセスは不要。
- MIDASは`libmidas.a`と`mfe.o`を静的リンク
  （`easiroc_frontend/Makefile:8-16,49-54`、`hul_frontend/Makefile:2-6,20`）。
  ELFの動的依存は両frontendともlibz、libstdc++、libm、libgcc_s、libc、loaderで、
  RPATH/RUNPATHはなし。開発と本番のMIDAS archive/mfe.oのhashは異なる。
  本番ヘッダで主要翻訳単位のsyntax-only検査は成功したが、完全build/link、
  ABI/実行時動作・動的ライブラリのversion適合は今回未検証。本番用依存での再buildが必要。
- EASIROCのIPは両ODBとも192.168.10.40、Info上UDP/RBCP4660、TCP24。
  開発も同じ装置を所有し得るため、本番起動前に開発feeasirocの通信停止と所有権移譲が必要。
  HULは192.168.10.60、UDP4660/TCP24。宛先IPが異なるので同じport番号自体は競合しない。
  全ネットワークのIP重複、実配線・firmwareは今回は検証していない。
- 両Settings/Enabled=true。ASIC1 DiscriminatorDACCodeは開発601/本番600、
  ASIC1/2 InputDACは開発全ch480/本番全ch350。共通の他Settings値は比較で一致。
  開発にのみSettings/HV、Probe、SelectableLogic、TriggerWidthが存在。
  本番LastAppliedはlegacy ASIC-only、開発はExtendedを含む別内容。
  保護する正しいpathは `/Equipment/EASIROC/Settings/ASIC1,ASIC2` と
  `/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied`。
  `/Settings/ASIC`やequipment直下の`LastApplied`を退避対象と取り違えない。
  Devices registryは両方とも未作成。
- 初期化は既存キーを保持し、欠損を作成するが、ensure自体は既存型を検査しない
  （`easiroc_odb.cxx:66-76,572,657,679`）。後続読取りで失敗し得るため事前schema検査を追加する。
  legacy LastAppliedにExtendedがなくても読める（同`:950-960`）。
  Extended=trueの不完全snapshotは拒否する。既存ASICを零で再作成したり、
  開発LastAppliedをコピーしたり、Extended=trueを偽装しない。
  拡張Settingsを使うならProbe/Logic/Widthを全項目の承認済み値で揃え、部分追加しない
  （同`:1276-1287`）。legacyを維持する初回導入と拡張設定の適用は分ける。
- 開発HV CurrentLimit_uA=20、HardwareAccessEnabled=true。
  観測時はEnabledState=OFF、InProgress=false、ActiveRequestId=0、MeasurementValid=true、
  約0.12078 V/0.0364 µA、MonitoringStatus=OK。ただし状態は稼働frontendの申告であり、
  装置状態の直接読戻しではない。本番はSettings/HV・Variables/HV・Commands/HVすべて欠損。
  **本番HV状態は不明**。frontend不在や低電圧の開発観測だけで本番再起動を許可しない。
- 新規HVキーはCurrentLimit20 µA、HardwareAccessEnabled=falseで作成する
  （`easiroc_odb.cxx:1565-1575`、`easiroc_hv.h:7-19`）。HV測定もMADC register書込みを伴う
  （`easiroc_hv.cxx:15-55`）ため、単なる読取り監視とは扱わない。
  既存アクセス許可がtrueなら通常loopは測定し得る。新規導入時はfalseを確認し、
  欠損値を作る場合もASIC Settings/LastAppliedとは独立させる。
- HVはSTOPPED・遷移なし・登録/Enable・ASIC非Applyで操作、Enable時は有効LastAppliedと
  全ch InputDAC有効等を要求する（`easiroc_odb.cxx:1602-1631`、`easiroc_hv.cxx:83-129`）。
  昇圧4 V/step、通常降圧10 V/step・実測±5 V/最終≤5 V確認、Emergencyはshutdownを直接要求。
  CurrentLimit超過はramp中に保護降圧へ移るが、定常時は警告であり、
  ソフトウェア常時自動遮断の保証ではない（`easiroc_hv.cxx:148-217`）。
  参照firmwareでのEnable方式確認は実装済みfirmware同定の証明ではない。
- 制御用freshはramp2秒/通常65秒。表示用は段階処理2秒以内かつ測定年齢6秒以内、
  通常65秒。LastUpdateUnixTimeMsは成功測定時刻のみで更新し、GUIもmsで判定する。
  Fault/無効/未来時刻/HTTP失敗/IP不一致はUnknown。
  `easiroc_hv.cxx:72-81`、`easiroc_odb.cxx:1672-1675`、`daq_monitor/web/easiroc.js:633-641`。
  GUIだけ更新して旧frontendの2秒Invalidを解消したとは扱わない。
- frontend起動はEnabledなら通常診断通信を開始し、終了もDAQ cleanupを行う
  （`feeasiroc.cxx:899-949`）。HV runtimeは再起動でUnknownへ戻り、古い要求は再実行しない
  （`easiroc_odb.cxx:1590-1597`）。再起動条件はSTOPPED/遷移0、Apply/HV処理なし、
  装置の安全状態を別途確認済み、開発所有権解除済み、未処理mailboxを確認済みであること。

### HULの実装・不足

- 参照調査は`hul_frontend/REFERENCE_ANALYSIS.md`、再利用評価は`CODE_REUSE_ANALYSIS.md`、
  現行計画・試験記録は`IMPLEMENTATION_PLAN.md`。公式Scaler/CoreLibのregister mapと
  ガイドに基づき、EASIROCの通信手順は流用していない。
- 実行対象`hul_frontend/bin/fehul`、専用起動wrapper/unitは現時点でなし。
  Makefileは`make -C hul_frontend all check`、MIDASSYSの既定が開発絶対パスなので
  本番では必ず本番MIDASSYSを指定する。依存のofficial/*.ccはfrontend内でbuildする。
- `/Equipment/HUL Scaler 1`、周期ID10、最終ID11、SYSTEM、Period1000 ms、RO_RUNNING|RO_ODB、
  Log history=1、HCUM[64]/HDIF[64]/HVAL[15] DWORD
  （`fehul.cxx:150-162,288-334,362-374`）。設定初期値はIP .60、RBCP4660、TCP24、
  EnableBlock3、FreshnessMs2000、DrainMs2000。既存値を保持し型・範囲を検査する
  （同`:38-45,123-147`）。**TriggerSelectというODBキーはない**。
  SelectTrigger=ExtL1(1)、NIM IN1、Hdrst0、SpillGate/Busy default(7)をBORで固定設定する
  （同`:427-443`）。可変化するなら追加設計・範囲検証が必要。
- 新Common/Enabledの初期値はTRUE。frontend_init自体は通信しないが、loopはEnabledなら
  停止中でも2秒周期RBCP監視を行う（同`:331-374,547-572`）。
  本番初回登録の前に承認済みschemaでCommon/Enabled=falseを用意し、初回起動から
  通信しない設計にする。起動後にOFFへ変える方式には通信開始の隙間がある。
- EnabledのBORはGate OFF→TCP接続/旧byte有限排出→EVB/Counter reset→設定/読戻し→
  worker→Gate ON。失敗はGate OFFを試みBOR失敗。
  Disabledなら通信せず参加を記録してEORで解除する（同`:396-474`）。
  Common/EnabledはRUN中read-onlyになり、強制終了でmodeが残る可能性がある。
- TCPは部分受信/EOF/不正frameをfaultとしてworker終了し、RUN中の自動再接続はない
  （`hul_stream.cxx:82-141`、`fehul.cxx:257-285`）。RBCP監視のComm OKはTCP受信成功を意味しない。
  無到着はIdleであり、valid/fresh=0・fault=0の周期イベントを出し続け得る。
  RUN停止を自動要求する実装ではない。診断情報不足で「無受信」と「worker未進行」を
  切り分けられない。再接続は装置/Gate/frame境界/RUN帰属を確定する復旧手順が必要。
- EORはGate OFF、DrainMs内のframe処理、最終ID11 enqueue/flush、重複防止。
  Gate/flush失敗はEORエラー（`fehul.cxx:477-539`）。pauseは未対応（同`:542-546`）。
  flush成功はディスク保存証明ではない。受信faultがあってもGate OFF/送信が成功すれば
  EOR自体は成功し得るため、HVAL/fresh/fault/acceptedも必ず検査する。
- 開発の既存RUN186ファイルを今回再解析：SHA256
  `7588952ba6bc7fefadd06b9aa74934774b794f9a1400ab9a5d7cfcd2d36def63`。
  ID10=11、ID11=1、VME ID1=108、RUN/Final整合は成功。
  全HUL記録valid=0/fresh=0、accepted=0/self=0/fault=0、増加chなし。
  これは無効snapshotの保存確認であり、実Scalerの正常取得ではない。
  `IMPLEMENTATION_PLAN.md:163-195`の未達記録を独立に再確認した。
- 過去記録上はRBCP EnableBlock読出し、VME共存BOR/EOR、ID11保存、mockのTCP分割/
  EOF/不正長/10,000frame、snapshot/Bank/guard/monitor、GUI、Enable access-mode試験が成功。
  実frame/wire順、NIM波形・差動入力・firmware/DIP、L1認識、TCP送出、backlog鮮度、
  入力rate限界、Gate OFF後のRUN帰属、EASIROC共存、History実記録は未完了。
  次段階でworker heartbeat/exit理由、recv回数/byte/Idle/EOF/parse/errno、先頭header、
  BOR排出byte等の診断を追加し、許可された開発実験で原因を解決する。
- 本番Logger channel0はactive/Disk/MIDAS/SYSTEM/EventID=-1/TriggerMask=-1/lz4。
  設定上ID10/11を受け入れ得る。既存Equipmentには10/11なし。ただし新データの解析側対応、
  History保存、本番出力での最終eventは未検証。既存ファイルの書換えは不要。
  monitorのCanStartはVME/EASIROCだけ、HULEventsは-1の設計
  （`daq_monitor/status_policy.cxx:151-174`、`hul_frontend/IMPLEMENTATION_PLAN.md:119-121`）。
  HUL単独STARTや故障時START抑止を保証しない。一方、fehulを有効参加させれば
  BOR/EORエラー・buffer負荷が既存DAQ全体に及ぶ。共用画面追加だけと同一視しない。

### 将来の反映計画（今回未実施）

1. **安全確認とリリース分離**：別途反映許可後、実DAQホストのdaq shellで実施する。
   STOPPED、Transition/Requested transition=0、運用上の全START抑止、client/PID、
   IP所有権、装置安全状態、Apply/HV mailbox、空き容量、対象commit/branch/cleanを確認。
   開発feeasirocが同じIPを使用中なので通信所有権を移譲する。HULも同様に単一所有者とする。
   EASIROC用とHUL用のリリースmanifest・journal・退避先・受入条件を独立させる。
   main全体pullだけで独立反映を達成したとは扱わず、各リリースの変更集合を確定する。
2. **バックアップ**：全ODBは災害復旧用に保管するが通常復旧で丸ごとloadしない。
   対象別に正規旧binary、wrapper、実配信HTML/JS/CSS/mock-dataと依存asset、
   commit/hash/mode/symlink、Programs/Customの値と事前不存在を保存。
   EASIROCはSettings全体、Variables/ASICSlowControl/LastApplied、indeterminate、
   Commandsとrequest/handled ID、HV状態・設定・既存registry。
   HULはEquipment全体/Common access mode、Programs、Logger/History設定を保存。
   MIDAS依存版/hash、unitと元の稼働状態も記録し、既存データ/Runlog/ELOGは保全する。
3. **スクリプト変更**：対象指定・本当に副作用のないplan/dry-run・対象別journal/backup/
   rollbackを追加する。pull/build失敗前から旧assetを保護し、Webとbinaryの組合せを管理。
   EASIROCはHV/拡張/LastApplied型・配列長・全snapshot検査、
   `/Custom/EASI Slow Control`の追加、旧値不変の検査、check-webを追加。
   既存checkにはweb試験がない（`easiroc_frontend/Makefile:76-103,251-253`）。
   HULは実機取得問題解決後にbuild/check、fehul wrapper、Programs、存在/状態/hash検査を追加。
   初回はoptional・auto start/stop/restart=false、明示起動とし、
   自動起動で新たな装置通信やRUN参加を始めない。systemd方式を選ぶならunit導入を別途明示する。
   対象外VME/monitor/analyzerを無条件停止する既存全体方式を対象別方式へ改める。
4. **配置・schema・起動**：本番依存で事前build/testを通した成果物とGUI一式を検証して切替。
   EASIROCは不足キーだけを型確認付きで作成、HVアクセスfalse、旧ASIC/LastAppliedを保持。
   legacyのまま開始できる構成とし、拡張値やHardwareAccessEnabledを開発からコピーしない。
   本番feeasirocは現在不在なので「元状態復元」と別の、承認された新規起動段階が必要。
   HULは将来、Enabled=falseを初回起動前に作成、固有ID10/11、SYSTEM、Settingsを読戻す。
   Commonの既存値をMFEが保持するため、コード既定値だけを信頼しない。
   起動wrapperはHOME=/home/daq、本番MIDASSYS/exptab、experiment=daqを明示しremote変数を解除。
   初期化成功、client単一、通信抑止、既存client不変を確認後にのみ次段階へ進む。
5. **受入**：8081のexperiment/Custom Path/登録URLとcache回避asset hash、実行binary hash、
   元Settings/LastApplied不変、新schema、mailbox非再実行、GUI権限制御を確認。
   EASIROCは新frontendとGUIの組合せ、ms鮮度/Fault/Unknown/再起動後状態を確認。
   HV実機受入は別途許可・安全条件の下で行い、現場firmware/Enable時のDAC挙動、昇降圧、
   CurrentLimit、Emergency、通信失敗と定常監視の限界を確認してから操作を開放する。
   HULはまず開発で10 Hzの完全frame/HCUM増加/HDIF/valid/fresh/self/欠損推定を満たすこと。
   切断・無到着・再開・BOR失敗/STARTABORT/EOR失敗・残留frame/RUN帰属も検証する。
   その後の本番受入RUNは別許可とし、ID10/11・RUN/Final・ディスク保存・History・
   VME/EASIROC共存を確認する。GUIのComm OKやEventsだけで合格としない。
6. **異常時復旧**：STARTを抑止し、RUN状態・HV/Gate状態を確定して対象だけを停止する。
   通信中/状態不明でbinaryやODBを強制的に巻戻さない。対象旧binary/GUI/wrapperと
   対象Programs/Customを戻し、新規キーの事前不存在も復元する。
   EASIROCで実際にApply/HV操作後なら物理状態はファイル復元では戻らない。
   LastApplied/request IDを過去へ巻戻して要求を再実行させず、現場で状態を照合する。
   HULはGate OFF確認後、必要時STOPPEDでEnabled access modeを元に戻し、
   新規fehulを外して新規Equipment/Programsを対象限定で撤回する。
   記録済みデータ/Historyは残し、他frontend/Logger/mhttpdの状態を変えない。
   全ODB restore・git reset --hard・journal削除を通常復旧の代用にしない。

### 今回の検査の範囲

`bash -n`でstep1/2、Python AST parseでstep3/4の構文を確認し成功。
開発のfeeasiroc.cxx/easiroc_odb.cxxとfehul.cxx/hul_bank.cxxを本番MIDAS includeで
`g++ -std=c++17 -Wall -Wextra -Wpedantic -fsyntax-only`検査し、警告・エラーなし。
出力ファイルを作るmake/linkやテスト再buildは読取り限定のため実行していない。
既存EASIROC記録/ログではHV429 checks・ODB adapter9941 checks・GUI164 checks成功
（`easiroc_frontend/docs/easiroc_slow_control.md:3528-3552,3633-3641`）。
HULの既存build/mock成功は`IMPLEMENTATION_PLAN.md:178-190`に記録。
今回新たに実行したデータ検証は保存済RUN186の読取り解析であり、実機再試験ではない。
現時点の完全な本番build/test、HV実機受入、HUL正常取得の証明は未取得である。

HULの既存設計Markdownはroot `.gitignore:98`の対象で、更新は通常のgit diffに出ない。
今回の中心的な調査結果はtrackedの本READMEに保存し、HUL/EASIROC文書から参照した。

## EASIROC専用反映スクリプト（2026-11-05実装、未反映）

`deploy_easiroc.py`は既存4段階の全体更新と別の入口で、更新対象を
`easiroc_frontend/bin/feeasiroc`と`daq_monitor/web/`の
`easiroc.html`、`easiroc.js`、`easiroc.css`、`easiroc-mock-data.js`の5ファイルに固定する。
Git pull、VME/HUL/monitor/analyzerのbuildや停止、systemctl start/stop、実機測定、HV操作、
RUN遷移は行わない。既存wrapper、MIDAS/共有ライブラリ、DAQ共用HTML/JSは更新しない。
main全体をpullする既存反映とは混ぜず、承認済み配布物を別directoryから読み込む。

### 配布物の作成（開発領域のみ）

```sh
python3 scripts/deploy/prepare_easiroc_release.py \
  --output easiroc_frontend/build/easiroc-release-YYYYMMDD \
  --midas /home/daq/midas/midas_src \
  --node easiroc_frontend/build/tools/node-v22.16.0-linux-x64/bin/node
```

この処理は開発repo以外では拒否する。本番MIDASは依存として読取りのみ。
指定した未作成の開発build配下へmakeで専用object/binary/testを作成し、
`feeasiroc check check-web`成功後にGUIを集めて`manifest.json`を生成する。
通常の開発binaryを置換せず、本番のファイルやODBへは接続しない。
MIDASの`lib/midas-targets.cmake`が公開するリンク依存を使用する。
今回の本番archiveはSQLite等が必要で、既存Makefileの既定リンク指定だけでは不足した。
この追加指定は配布物作成時だけ行い、既存frontend Makefileは変更していない。

manifestは5ファイルのSHA256、libmidas.a/mfe.oのSHA256、リンク指定、source commitを持つ。
途中失敗ではmanifestを生成せず、そのdirectoryは配布物として使用できない。
本番へ適用する前に、daqユーザーが配布物全体とスクリプトを読める権限を確認する。
manifestは改ざんに対する署名ではない。作成元・保管・受渡しの信頼性は運用で確保する。

### 各モード

本番での操作は承認後に実DAQホストのdaq shellから行う。以下は将来の実行例で、
今回は`--apply`と`--rollback`を実行していない。
スクリプト自身は開発repoから読めるので、本番checkoutをpullする必要はない。

```sh
# 読取り専用。失敗時もファイル/ODB/client/実機は変更しない。
python3 /home/nagao/midas/midas/online/scripts/deploy/deploy_easiroc.py \
  --check --release /home/nagao/midas/midas/online/easiroc_frontend/build/easiroc-release-YYYYMMDD \
  --safety /path/to/easiroc-safety.json

# 別途反映許可後のみ。
python3 /home/nagao/midas/midas/online/scripts/deploy/deploy_easiroc.py \
  --apply --release /home/nagao/midas/midas/online/easiroc_frontend/build/easiroc-release-YYYYMMDD \
  --safety /path/to/easiroc-safety.json

# 別途復旧許可後のみ。表示されたバックアップを明示する。
python3 /home/nagao/midas/midas/online/scripts/deploy/deploy_easiroc.py \
  --rollback --backup /home/daq/midas/midas/backups/easiroc-日時-PID \
  --safety /path/to/new-easiroc-safety.json
```

モードは必須・排他であり、引数省略時に反映しない。
`--check`のRPC objectはdb_get_values/db_key/db_copyだけを許し、書込みRPCは送信前に拒否。
lockやバックアップdirectory、temp file、bytecodeを作らず、buildやclientの起動も行わない。
HTTP読取り・systemctl show・/proc読取りで確認する。HTTPサーバ側の通常access log等は
サーバ運用に依存するが、このスクリプトはログの作成・変更要求を送らない。
`--check`成功はその時点の確認で、applyは同じ条件を再確認する。
実ホストのPID/mount namespaceを確認できないsandboxでは、checkも成功扱いにしない。

### 安全確認記録と拒否条件

ODBのEnabledStateはソフトウェアの申告であり、frontend不在時の古い値や欠損値から
HVがOFFとは判定しない。**全モードで現場が別手順で取得した外部安全確認記録を要求**する。
スクリプトはその確認のための新しい実機通信を行わない。
記録の例（`verified_at=0`は意図的に期限切れであり、このまま使用できない）：

```json
{
  "experiment": "daq",
  "ip": "192.168.10.40",
  "verified_at": 0,
  "hv_off": true,
  "voltage_v": 0.1,
  "current_ua": 0.01,
  "operator": "実際の確認者",
  "method": "実際のOFF確認・残留電圧/電流確認方法と一次証跡の所在",
  "starts_inhibited": true,
  "development_restart_inhibited": true
}
```

実際に確認したUnix秒を指定する。未来・300秒超の記録、IP/experiment不一致、
OFF未確認、電圧>1 V、電流が20 µAまたは既存CurrentLimit以上の場合は拒否する。
この記録は人の確認の明示であって、物理状態をスクリプトが独立測定・証明するものではない。
全START入口・GUI操作・他ODB writer・開発frontend再起動を保守中排除した状態でのみ作成する。
復旧でも新しい記録が必要。電源断等の代用としてフラグだけを記入しない。

追加の条件は次のとおり。

- 8081 experiment=daq、State=STOPPED、Transition/Requested transition=0。
  8181 experiment=daq-devを読めること。開発feeasiroc clientは不在であること。
  clientだけでなくhost /procで未登録/重複のfeeasirocがないこと、稼働本番exeが既存binaryと一致すること。
  開発と本番のIPも表示する。開発frontendをスクリプトから停止することはない。
- 既存HardwareAccessEnabled=trueは拒否する。既存値をfalseへ自動変更しない。
  新規キーだけfalseで作成する。現場で既存許可を解除する必要がある場合は別途明示的な操作とする。
  稼働本番のHVがON/処理中なら外部記録があっても拒否。抑止状態で再起動後のUnknownは
  外部安全確認で補完する。ASIC Apply/BufferClear/HVの未処理要求・active操作も拒否する。
- ASIC1/2 SettingsとLastAppliedの必須型・配列長を検査し、欠損を既定値で埋めない。
  ApplyAtBOR=true、不完全なProbe/SelectableLogic/TriggerWidthやExtended snapshotは拒否する。
  legacy ASIC-onlyと完全な拡張schemaは保持できる。追加Devices registryがある構成は
  複数装置のHV/通信所有権確認が必要なため、この版では拒否する。
- `/Custom/Path`が本番web path、Programs Start commandが既存本番wrapper、wrapper内容が
  既知の版で、Auto start/stop/restart=falseであること。独自起動方式は勝手に置換しない。
- 固定ファイル一覧とSHA256、既存binary、MIDAS依存hash、保存先の存在/アクセス権、
  バックアップ先・配置先の空き容量を検査する。symlink経由の対象更新を拒否する。
- mutationはdaq@nexdaq1、HOME=/home/daq、実mhttpdと同じPID/mount namespaceだけで許可。
  既存deploy1と同じruntime/deploy.lockを使用し、全体反映の未完了journalも拒否する。
  EASIROCの未完了journalがある場合は新規applyを拒否し、先に復旧する。
  既存deploy1にも独立EASIROC未完了journalの拒否検査だけを追加した。その他の既存処理は維持。

### 反映・復旧の範囲

applyは最初に`backups/easiroc-UTC日時-PID/`を作り、旧5ファイル（不存在も記録）、
既存起動wrapper、EASIROC全体・Programs/feeasiroc・Customのdb_copyを退避する。
`journal.json`に日時、旧ファイルのmode/SHA256、ODB JSONのSHA256、配布manifest、
元のclient状態、既存保護値、安全確認記録、処理段階・各操作intentを保存する。
ODB backupはJSONであり、既存全体反映のASCII `.odb`と混同しない。

本番feeasirocが元々接続していた場合だけ、そのclientをshutdownして更新後に既存Programsから
再起動する。元々不在なら不在を維持し、新規起動は別途許可された運用操作とする。
起動後はclient登録だけでなく、新frontendのHV抑止heartbeatの進行も確認する。
旧版へ復旧する際はHVを持たない旧binaryも想定し、client再登録までを自動確認の範囲とする。

各ファイルは同じdirectoryのtemp fileからfsync/replaceする。5ファイル全体の同時切替では
ないため、切替中のGUI利用を保守手順で排除する。
不足するHV設定・HV要求の安全な既定値・LastHandledRequestIdとCustom登録だけを作成する。
不足親directoryもintentを記録する。HV計測値やASIC適用成功を捏造しない。
InfoやVariablesの他の不足キーは通常frontendの初期化・状態公開に任せる。
既存ASIC Settings/LastAppliedは型/長さ/値/access modeを比較し、変更を検知したら中断する。
新frontend自身の通常の状態・heartbeat・Info更新は発生する。
既存のSettings値、LastApplied、Programs、Custom/Pathを設定目的で書換える処理はない。

途中失敗では他サービスを操作せず、journalと退避先を残して失敗する。
安全状態を再確認できないまま自動再起動/自動復旧は行わない。
rollbackは指定backupのhashと現在ファイルが旧版/今回配布版のいずれかであることを確認し、
feeasirocだけを停止して旧ファイル・mode・事前不存在を復元する。
対象外client PIDの変更や既存ASIC Settings/LastAppliedの差分があれば自動復旧を拒否する。

ODB全体/subtreeをloadする処理はない。成功確認済みで現在値も初期値のままの新規キーと
空の新規親だけを削除する。RPC失敗で成否不明のintent、frontendが更新したstatus/request ID、
人が変更した設定・登録は削除せず、`retained_odb`として報告・journal保存する。
物理ASIC/HV状態とrequest ID/LastAppliedの巻戻しは行わない。
wrapperや起動設定を外部で変更した場合も無条件復元せず、現場で差分を照合する。
rollback済みbackupの再実行は拒否し、不要な再起動を防ぐ。

### 検証結果と残る制限

- 専用モック試験22件、既存を含むPython試験69件成功：正常apply/rollback、元稼働状態維持、5ファイル各段階の失敗、
  ODB作成/応答失敗のintent保全、停止失敗・起動失敗からの復旧、改変file/backup拒否、
  ASIC/LastAppliedと非対象file/client保護、変更済み追加ODB値保持、完全/不完全拡張schema、
  再実行制御、容量不足・symlink・安全条件・manifest/依存不一致、check無書込みを確認。
- 本番MIDASを読取り依存として開発`build/deploy-release-validation-v3/`でmake/build/link、
  check/check-web成功。HV429、ODB adapter9941、GUI164 checksとその他既存C++試験成功。
  最終buildにコンパイラ警告・エラーなし。最初はSQLiteのリンクエラー、次にMakefileの
  `./`付きtest起動と絶対BIN_TEST_DIRの不整合で失敗したが、配布物作成script内で解消した。
  この修正は既存開発binary・本番ファイルを置換していない。
- 実8081への読取りschema検査は成功し、不足する安全な追加キー9個を確認。
  作成済みreleaseで`--check`を実行し、hash表示後にsandboxのhost namespace不可視で
  拒否することを確認した。実環境でのcheck成功・apply/rollback成功は未検証。
- 実機HV保護、昇降圧、現場firmware、frontend再起動、実ホストでのprocess切替、
  本番ブラウザ操作、配布binaryの本番実行は今回検証していない。HULは対象外。
- ODB HTTP APIには複数リクエストにまたがるcreate-only/CAS transactionがない。
  各段階で再読取り・RUN検査・intentを記録するが、別writerや別STARTとの競合を原子的に
  防ぐものではない。lockはこのscriptと更新済みdeploy1同士の排他であり、
  操作者・他client・古い未更新deploy1に対しては保守中の操作排除が前提。
  独立切替後に従来の全体buildを行う場合、そのbuild側のMIDAS依存リンク指定も別途確認する。

再検証コマンド（開発領域のみ）：

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest scripts.tests.test_deploy_easiroc
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/tests -p 'test_*.py'
bash -n scripts/deploy/deploy1_init.sh scripts/deploy/deploy2_impl.sh
```

## deploy1へのEASIROC判定追加の影響調査（2026-11-05、調査のみ）

結論：**EASIROC未反映・journalなしは追加判定で拒否されない。一方、歴代journalの
一律検査には過剰な連動と欠損時の検出漏れがあり、更新対象に応じた設計修正が必要。**
さらに、専用反映で追跡GUIがcheckoutと異なると、journal判定より前のGit clean検査で
正常完了後も全体反映が止まる。現行の全体反映はEASIROCも常に対象であり、判定だけを削除して安全な
VME/monitor/analyzer限定反映にできるわけではない。
本節は前節の実装を再評価した結果で、スクリプト修正・反映は行っていない。

### 対象差分と確認した版

- 開発版`deploy1_init.sh`のHEADとの差分は7行追加だけ。
  `pathlib.Path`のimportと、`/home/daq/midas/midas/backups/easiroc-*/journal.json`を
  全件読む処理（`scripts/deploy/deploy1_init.sh:48-54`）。
  SHA256は`efe774a97c0979e642ac7bf9df588b752061f3191b8a3530b399b6a2e306ad1c`。
- 本番ファイルは読取り比較のみ実施。現在の本番`/home/daq/.../deploy1_init.sh`のSHA256は
  `6d884f7470cc32cb927a9e9347310b8369bede5efec6dbad673e1f93dda3d2a2`で、
  開発HEADの変更前と一致し、追加判定は未反映。以下の新しい拒否条件は開発版への評価で、
  現本番ですでに発生している問題とは主張しない。
- ODB/8081/8181への照会、サービス・実機・HV操作、本番scriptの実行は今回行っていない。
  既存の作業差分は保持し、本節追記とルートcodex.log保存のみを行った。

### 追加された拒否条件の具体的な動作

一致した各journalを`json.loads(journal.read_text())`で読み、
`state.get("phase")`が文字列`complete`または`rolled_back`以外なら例外で終了する。
1件でも不合格なら後段へ進まない。最新日時/現在の対象root/実際の変更範囲は見ていない。

| journal・ファイル状態 | 追加判定の結果 | 意味・限界 |
| --- | --- | --- |
| backupsなし、空、対象journalなし、EASIROC専用script/配布物なし | 通過 | 未反映を拒否する条件はない。専用scriptのimport/起動やmanifest参照もない |
| `phase=complete` | 通過 | 正常完了のphaseだけを信用。配布物・旧binary・ODB backupの存在/hashは検査しない |
| `phase=rolled_back` | 通過 | `retained_odb`が残っていても通過。完全なODB原状復帰の証明ではない |
| `backed_up`、`installing`、`failed`、`rolling_back` | 拒否 | 未完了の全状態。停止/配置前に失敗した記録も区別しない |
| 古いfailedと新しいcompleteが共存 | 拒否 | 新しい成功があっても、歴代のfailedが1件あれば止まる |
| `phase`欠損/未知、JSON破損/空、JSONが配列/null | 拒否/例外終了 | phase以外の不整合も例外で全体更新を止める。JSON構造異常は専用診断にならない |
| journalがdirectory、読取り権限なし | 例外終了 | IsADirectoryError/PermissionError。VME等の更新目的と無関係でも止まる |
| easiroc-* directory内に旧ファイルだけ残り、journal.json欠損 | 通過 | globに一致するjournalがなく、未完了の可能性を検出しない |
| journal.jsonが切れたsymlink | 今回のPython/globでは通過 | 実ファイルが存在しないため走査対象外。完全性検査とは扱えない |
| phaseはcompleteだがroot/formatが不正、backup資材が欠損 | 通過 | phase以外の整合性を検査しない |
| 別rootの記録でもphase=failed | 拒否 | root/componentを確認しないため、関係のない保存記録でも妨害し得る |

通常の専用scriptは`backups/easiroc-UTC日時-PID/`を作り、
`backed_up → installing → complete`、捕捉した失敗で`failed`、
復旧で`rolling_back → rolled_back`へ進める
（`deploy_easiroc.py:394-420,441-480,508-544`）。
backup作成後・journal保存前の失敗では、directoryだけ残る可能性もある。
`installed=[]`や`actions=[]`だけで無変更とは判定できない。
frontend停止は`installing`保存より前で、停止intentを独立記録していないためである
（同`:443-451`）。現在の記録から機械的に安全な「失敗前だった」と決めない。

### 各運用ケースへの影響

以下の「通過」は**追加したjournal判定だけ**の結果。host/ユーザー/main/clean/STOPPED、
unit/設定schema、build/test等の従来条件を満たさなければ全体反映自体は失敗する。

| ケース | 実行可能性・拒否条件 |
| --- | --- |
| 1. EASIROC未反映でVME/monitor/analyzerを更新 | journalなしなら追加判定は通過。ただし既存scriptには対象選択機能がなく、EASIROCを完全に除いた更新はできない。EASIROC binary/Settings等がない初期状態は従来側の条件で失敗し得る |
| 2. 専用反映が正常完了 | すべての一致journalがcomplete/rolled_backなら通過。過去のfailed・壊れた記録が別に残ると拒否。GUIがcheckoutと異なれば、その前のGit clean検査で全体反映は拒否される。Git条件を満たして先へ進めてもEASIROCも再build/必要時再起動するため、専用配布版の維持は保証されない |
| 3. ロールバック済み | そのjournalがrolled_backなら通過。過去の未解決記録、復旧中断のrolling_back、JSON破損があれば拒否。旧GUIが元のGit版と一致すれば専用反映によるtracked差分は解消する。元からの差分・復旧後の別編集があればGit条件で拒否。残したODBキーはjournal判定では調べない |
| 4. 関連ファイル/記録の欠損・不整合 | 読めるjournalのphase不正やJSON破損は拒否。journalそのものの欠損、phaseが許可値で資材だけ欠損は通過。資材完全性のチェックではない |
| 5. 初回構築・既存環境更新 | 専用反映前提を新設したわけではないが、従来script自体が既存実験・4サービス・Settings等を前提とし、ゼロからの構築用ではない。旧bootstrapから更新する最初の呼出しでは新判定が適用されない場合がある |

既存VME/monitor/analyzerの経路は維持されている。
`deploy2_impl.sh:205-222`はVME/EASIROC/monitorのmakeとanalyzerのcmake/build、
各check/ctest/Python試験を常に実行する。EASIROC client不在でもEASIROC buildは省略しない。
同`:175-243`は元々動いていたmonitor/analyzerとfevme/feeasirocだけを停止・復帰する。
mhttpd/mloggerは停止対象外。追加7行がこれらのコマンドを書き換えたわけではなく、
そこへ到達する前の入口条件を追加した変更である。

ODB側も`deploy3_configure_live_odb.py:295-305,369-381,473-476`でEASIROCのPrograms/
Settings検査を含み、`deploy4_check_live.py:87-99`もfeeasiroc binary/wrapperを要求する。
従来から「EASIROC未使用ならVME等だけに限定する」構造ではない。
また、専用配布物作成のMIDASリンク依存補完（`prepare_easiroc_release.py:20-28,44-49`）は
従来`deploy2_impl.sh:210`のmakeへ自動継承されない。全体buildで同じリンク不足が再発する
可能性は前節で既知であり、今回の追加7行に起因する新しい障害とは分けて扱う。

### 正常完了でも発生するGit clean条件との競合

**独立運用上の具体的な問題がある。** 専用applyはGUIをGit管理下の本番checkoutへ
直接置換する（`deploy_easiroc.py:452-464`）。既存deploy1はjournal検査に到達する前の
`git diff --quiet`と`git diff --cached --quiet`でtracked変更を拒否する
（`deploy1_init.sh:41-42`）。本番で4つのeasiroc GUIファイルが追跡対象であることを
`git ls-files`の読取りで確認した。

したがって配布GUIの内容が本番checkoutと異なる場合、phase=completeでも
VME/monitor/analyzerを更新する全体手順は入口で停止する。
これは追加7行自体の副作用ではなく、**新しい専用配置方式と従来のclean条件との不整合**。
GUIがcheckoutとバイト一致する配布物なら、この理由では停止しない。
rollbackで退避した元GUIに完全に戻し、その元がcleanだった場合は解消する。

一時Git repositoryのindexに旧GUIを登録し、`git diff --quiet`が
旧内容0→専用配布内容1→旧内容復元0となることを再現した。commit/pushはしていない。
本番への反映や本番ファイルの書換えで再現したわけではない。
「正常完了のjournalさえあれば従来全体反映と共存できる」とする評価は不十分であり、
この配置/Git運用の整合も修正対象とする。

### 順序・再実行・独立運用の評価

1. 判定位置はbootstrapコピー/lock/main/clean検査の後、8081の状態照会・サービス照会・
   Git pullより前（`deploy1_init.sh:14-54,68-113`）。
   `set -e`下の代入中Pythonが失敗するとdeploy1が終了し、pull/build/ODB更新/サービス停止には
   到達しない。ただし、それ以前のbootstrap directory作成・lock file openは従来どおり発生する。
2. 既存の呼出し順`deploy1 → pull → deploy2 → build → deploy3 → restore → deploy4`は不変。
   しかし新判定はdeploy2より前なので、同commit/binaryで再起動を省く既存の再実行経路
   （`deploy2_impl.sh:269-282`）や全体反映journalの再開も、別のfailed記録で阻止され得る。
3. bootstrapは**pull前のdeploy1をコピー**して以後それを実行する
   （`deploy1_init.sh:14-20,103-113`）。旧版からpullで新scriptを取得しても、その回のbootstrapに
   新判定は追加されない。現行deploy2に独立EASIROC journalの同等検査はないため、
   最初の更新から拒否条件が効くとは保証できない。次回新deploy1を起動すると判定が有効になる。
4. 共有`deploy.lock`で並行実行を排除する設計は妥当
   （`deploy1_init.sh:30-35`、`deploy_easiroc.py:607-617`）。
   ただし操作終了後の歴代backupの状態で全体更新可否まで連動させており、独立運用は不完全。
   EASIROCを実際に変更する全体更新を未復旧状態で止める目的は必要だが、
   無関係な対象限定更新まで拒否する仕様としては過剰である。
5. complete/rolled_backを通すことはHV安全確認の代用ではない。
   全体反映は専用scriptのguard、HV抑止、外部安全記録、配布manifest検証を呼ばない。
   EASIROCに触れる全体更新の安全条件は別途満たす必要がある。

### 修正の必要性と最小限の修正案（提案のみ）

原則「EASIROCを更新しない作業は未反映に妨げられない」を実現するには修正が必要。
ただし現行はEASIROCに必ず触れるため、無条件拒否を単に削除したり、
feeasiroc不在なら無視したりする案は採用しない。

- **優先する最小の仕様変更**：更新対象を明示し、EASIROCに触れる場合だけ専用の未完了検査を
  行う。対象外ならEASIROC journalのために拒否しない。共有実行lockは維持する。
  対象指定はbuildだけでなく、frontend停止/復帰、Programs/ODB検査・更新、smoke判定にも通す。
  全体Git pullは配信中のEASIROC GUIも変更し得るため、非対象assetを更新しない配置方式にするか、
  incoming差分をpull前に確認してEASIROC差分がある更新を対象外モードでは拒否する。
  deploy2へ判定を移すだけでは、pullによるGUI変更を事前に止められない。
- **正常完了後のGit互換性**：配信するEASIROC artifactとGit管理sourceを分離するか、
  変更が承認済みmanifestの対象外artifactだけであることを照合して保護する方式へ改める。
  後者でも、pullによるGUI上書きやGit衝突を防ぐ配置手順が必要。
  全tracked差分の無条件許可、退避なしのcheckout/reset、live GUIを一時的に戻すだけの
  回避策にはしない。journal判定を限定するだけでは、このclean条件との競合は残る。
- **判定対象の限定**：歴代backup全件ではなく、現在の本番root/componentに対応する
  現行transactionを、固定のactive-stateまたは明確な参照先で管理する。
  記録のformat/root/phase・停止/配置intentを検証し、歴代の無関係なfailed/壊れた保存物を
  恒久的な全体停止条件にしない。EASIROC対象時に現行記録が壊れているなら安全側に停止する。
  記録削除・phaseの手編集を迂回手順にはしない。
- **導入順序の補強**：EASIROC対象更新では、旧bootstrapからの初回更新も含め、
  EASIROCファイル/GUI/ODBへ最初に触れる前に検査できる構成にする。
  条件変更時は`scripts/tests/test_deploy_easiroc.py:414-423`の全体拒否を固定した既存試験も、
  EASIROC対象/対象外・初回/再実行・欠損/破損の期待に合わせて更新する。

現行の全体更新だけを維持する間は、未復旧EASIROCを含む反映の拒否を残す判断が妥当。
「独立した非EASIROC更新にも対応済み」とは扱わず、対象分離と検査限定を一組で改修する。
今回、その改修は実施していない。

### 今回の検証範囲

既存script本文からjournal判定のASTだけを抽出し、開発build配下の一時directoryへ向けて
25ケースを実行、すべて期待どおりの通過/拒否/例外を確認した。
正常/rollback済み/全phase/未反映/歴代混在/破損JSON/型異常/資材欠損/誤root/読取り拒否/
directory/broken symlinkを含む。読取り拒否は例外注入、他は一時ファイルfixtureで検証した。
通常fixtureの内容hashが判定前後で不変であることも確認。一時directoryは終了時に削除した。

deploy2のbuild関数もAST抽出し、全コマンドを記録関数へ置換して10コマンドの経路を確認。
停止/復帰関数は同様に、EASIROC稼働/不在・他対象稼働/不在の3ケースをモック検証し、
元の稼働状態へ戻ることを確認した。実make、サービスコマンド、RPCは呼んでいない。
`bash -n`もdeploy1/2とも成功。これは本番scriptや全体反映の実行試験ではない。
今回はソースコード・テストコードを変更せず、再buildや実環境の反映試験も行っていない。

追加の一時Git検証1ケースでも入口条件を再現した。スクリプトのSHA256は調査前後で一致した。

## 既存デプロイのODB値移行規則の検証（2026-11-05、調査のみ）

**指定された4規則を満たす汎用的なODB移行処理は、現状の全体デプロイにはない。**
実装は「本番の限定した固定schemaをソース定義へ収束させる」もので、
「本番値を優先し、開発だけのキーを開発値で追加する」mergeとは異なる。
本節は仕様の検証結果であり、移行機能の実装・実施記録ではない。
スクリプト/テストのソース、本番ファイル、開発/本番ODB、サービス、実機/HVは変更していない。
frontend再起動・反映script・apply/rollback・RUN遷移も実行していない。
許可された本README追記とルートcodex.log保存のみを行った。

### 規則ごとの判定

| 要求規則 | 現状の結果 | 根拠 |
| --- | --- | --- |
| 本番に既存キーがあれば本番値を保持 | **部分的**。ASIC Settings等や通常の既存非管理値は保持するが、Programs/Custom/Runlogの管理値は既存でもソース固定値へ上書きする | `deploy3_configure_live_odb.py:197-214,289-314,498-501` |
| 開発だけの新規キーを開発値で追加 | **満たさない**。全体デプロイは8181を参照しない。既知の欠損固定キーをコードのinitialで作成し、それ以外の開発キーは取り込まない | 同`:13,97-161,197-206,473-505` |
| 本番固有キーを保持 | **対象外の一般キーは保持**。全ODB load/置換/不要キー全削除はない。ただし管理値は別方針で、管理Linksの未知名は拒否、既知名の異なるtargetは再作成する | 同`:262-286,498-503` |
| 型・配列不整合を無断変更せず検出 | **部分的**。開発版の既知Settingsは型/countを検査するが、HV/HUL/LastAppliedや未知キーを網羅しない。Runlog slip型の自動変換と固定STRING容量拡張は例外 | 同`:186-239,340-381,395-421` |

### 処理経路・バックアップ・初期化の違い

- `deploy1_init.sh`は8081でexperiment/RUN/frontend状態を読んでからpullし、deploy2へ進む
  （`deploy1_init.sh:56-113`）。開発ODBのexportや差分取込みは行わない。
  pullで更新された版のdeploy3が使用されるため、現在配置版と次回取り込む版を区別する。
- deploy2は本番ODBを`odbedit ... save`で固有のASCII `.odb`へ退避し、サイズ/権限を確認する
  （`deploy2_impl.sh:128-146`）。**backupは開発値の移行元ではなく、本番の復旧用保存物**。
  今回このbackup処理も実行していない。`restore()`はサービス/frontendの元の稼働状態を
  戻す関数であり、ODBをloadする関数ではない（同`:225-243`）。
  失敗時の自動ODB全体restoreはなく、journalを保持して再実行する
  （同`:269-304`）。同commit/binaryの再実行でもstep3は再度実施する。
- `specs()`にある欠損キーはinitialのData/空文字/0/false/本番path等で作成する。
  本番に既存の非管理値があれば、通常はその値を変えない。
  必須Settingsの欠損は開発版preflightで拒否し、開発値や既定値で修復しない
  （`deploy3_configure_live_odb.py:197-214,418-421,475-476`）。
- 既存`MANAGED_VALUES`は値が違えば`db_paste`する。例として
  `/Programs/feeasiroc/Auto restart=true`はfalseになる。既存値優先の仕様ではない。
  `/Custom/Path`、Loggerの本番path、ELOG portは異なれば拒否するので、
  本番固有値を常に受け入れる仕様でもない（同`:323-333,478-493`）。
- 固定キーの通常型/count不一致は拒否。短い固定STRINGは容量を自動拡張する。
  `/DAQ/Status/Runlog/EventSlipCount`だけはUINT64→INT64をdelete/create/pasteで
  自動変換し、表現可能な非負値を保つ（同`:209-239`）。これは既存の明文化された移行方針だが、
  今回の「型を自動変更しない」規則を例外なく満たすとは評価できない。
  調査時の両ODBの同キーは既にINT64であり、今回変換の必要性を確認したわけではない。
- 全体の固定schemaを先にすべて検査するtransactionではない。
  前方の欠損キーを作成した後、後方の型不一致で失敗する場合がある。
  DAQ Settingsのpreflight失敗は開発版で書込み前に拒否するが、固定spec全体の
  all-or-nothingを保証しない（同`:475-497`）。

### 開発版と現在の本番配置版

開発`deploy3_configure_live_odb.py`のSHA256は
`3cdbf695ac0514fc40991da812bf238fd54efdbbc40bedbf364d9876e089e052`、
本番`/home/daq/.../deploy3_configure_live_odb.py`は
`8c49f16f8f4e1d44b60eef03f15a864ec65e4e8159ea08190bf24a1c58559348`。
本番配置版には、開発版のVME/EASIROC/Analyzer Settings preflightがない。
本番版の`configure():348`はrequire_liveの次に本番path検査へ進み、
`inspect_schema():336`も固定specから検査する。一般の固定キー/管理値ポリシーは共通だが、
ASIC Settingsの型/count不整合検出まで現本番で実装済みとは言えない。

別入口の`deploy_easiroc.py`は全体デプロイから呼ばれない。
8181参照は安全確認/所有権確認用であって、開発値の移行元ではない。
不足HV等に20 µA/HardwareAccessEnabled=false等の**ソース既定値**を設定し、既存ASIC/
LastAppliedを検査・保持する（同`:168-181,194-250,252-289,423-439`）。
従ってこちらも「新規キーはすべて開発値」の実装ではない。

### 8181・8081の読取り比較

2026-11-05 07:10 JST頃、既存mhttpdへdb_get_values/db_copy/db_keyのみを送信した。
開発は8181 experiment=daq-dev、本番は8081 experiment=daq、両方State=1 STOPPED/遷移0。
Custom Pathは各々`/home/nagao/.../daq_monitor/web`、`/home/daq/.../daq_monitor/web`。
両実験のclientはmhttpd/Logger/ana/daq_monitorで、調査時frontend clientは不在。
ODBの残存Settings/Variablesを稼働中の装置状態の証拠にはしない。

比較は以下の対象subtreeのleaf単位で実施した。全ODBの全キーを網羅した比較ではない。
型/countと値を分け、STRING capacityは別比較とし、更新時刻metadataは値の差分から除いた。

| subtree | 開発leaf | 本番leaf | 開発のみ | 本番のみ | 共通leafの値差分 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `/Equipment/EASIROC/Settings` | 35 | 22 | 13 | 0 | 3 |
| `/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied` | 31 | 19 | 12 | 0 | 3 |
| `/Equipment/HUL Scaler 1` | 33 | 0 | 33 | 0 | 比較相手なし |
| `/Equipment/VME/Settings` | 41 | 41 | 0 | 0 | 4 |
| `/Custom` | 4 | 3 | 1 | 0 | 1 |
| `/Programs` | 111 | 138 | 19 | 46 | 16 |
| `/Logger/Runlog` | 21 | 21 | 0 | 0 | 0 |

上記の共通leafに型/countの差はなかった。CustomのSTRING capacityには差があり、
Path/DAQは開発128 bytes・本番256 bytes、Run Summaryは開発64・本番32 bytes。
これは型/count不一致と別の差分であり、容量差だけで既存本番値を置換しない。
本番固有の例は`/Programs/fe_vme_test/*`、`/Programs/fetest/*`等。
一般キーはこの種の本番専用情報を削除する経路がない。
旧`/Equipment/HUL`は両方欠損。`/DAQ/Settings`は開発に空directoryのみ存在し、
本番では欠損していた。空directoryの存在差をleaf数0の同一値とは解釈しない。

さらに開発版のread-only Settings検査（62 spec＋既存Analyzer設定検査）を
両ODBへ実行して成功した。これも**列挙対象が現在妥当**という確認であり、
HV/HUL/LastAppliedの網羅検証や移行成功の証明ではない。

### EASIROC・HULの具体的な結果

- **HV**：開発にCurrentLimit_uA=20（DOUBLE[1]）、HardwareAccessEnabled=true（BOOL[1]）があり、
  本番には両方ない。これらは全体step3の固定spec/Settings検査対象外で、開発値では追加されない。
  新frontendは起動後に欠損HVを20/falseのコード既定値で作成する
  （`easiroc_odb.cxx:1565-1575`）。その初期化はデプロイの開発ODB mergeとは別であり、
  frontend不在を維持した反映では起動による初期化も期待できない
  （`deploy2_impl.sh:235-242`）。
- **ASIC Settings**：共通設定は本番値を保持する経路。今回の値差は
  ASIC1 DiscriminatorDACCode=開発601/本番600、ASIC1/2 InputDAC=開発全ch480/本番全ch350。
  開発だけの13 leafはHV2、Probe6、SelectableLogic4、TriggerWidth1。
  全体step3はこれらの新規拡張値を追加しない。frontendの欠損ASIC初期化も
  コード既定値を使用し、既存キーは保持する（`easiroc_odb.cxx:66-76,679-767`）。
  ensure自体は存在時に型を検査せず戻るため、初期化と事前schema検査を混同しない。
- **LastApplied**：本番はValid=true、RequestId=2のlegacy snapshot、開発はValid=true、
  RequestId=4、Extended=true。共通値の差はRequestId/ApplyUnixTime/ASIC1 DAC。
  開発だけの12 leafはExtendedと拡張設定。全体step3は検査/移行しない。
  frontendは欠損legacy fieldを0/falseで初期化するコードを持つが、既存を保持し、
  開発のsnapshotを読み込まない（`easiroc_odb.cxx:572-655`）。
  LastAppliedは実際の送信記録であり、新規キーとして開発の成功履歴を移植してはならない。
- **HUL Settings**：本番のEquipment自体が欠損。開発SettingsはIPAddress=192.168.10.60
  （STRING、capacity64）、RBCPPort=4660、TCPPort=24、EnableBlock=3、FreshnessMs=2000、
  DrainMs=2000（後5項目はDWORD[1]）。全体deploy1〜4にHUL追加/起動の経路はない。
  将来fehulを別途起動した場合の初期値作成はコード既定値であり、開発ODBの複製ではない
  （`hul_frontend/fehul.cxx:38-45,123-147,362-374`）。
  `TriggerSelect`というSettings leafも現ODBにない。取得未達等の既存のHUL本番導入判定は
  本調査で変更していない。

### モックによる34ケースの確認

開発版・本番配置版のソースから定義/関数を分離して、require_liveだけをモック化し、
configureの実処理をメモリ内FakeRpcへ実行した。ネットワーク呼出しは明示的に禁止した。
本番scriptのmain、odbedit、サービス操作、frontend、実機は起動していない。
各版17ケース、合計34ケースが実装に対する期待結果と一致した。
ここでの「成功」は現行挙動の確認であって、要求規則への適合という意味ではない。

| ケース | 開発版 | 本番配置版 |
| --- | --- | --- |
| 既存非管理Comment値/既存ASIC InputDAC値 | 保持、mock書込み0（2ケース） | 同左 |
| 新規既知Comment、開発値をdev commentと想定 | 開発値ではなくコード既定の空文字で追加 | 同左 |
| 開発のみHVキー/HUL Settingsキー | 追加なし（2ケース） | 同左 |
| 本番固有Calibrationキー | 保持、書込み0 | 同左 |
| 既存Programs Auto restart=true | falseへ上書き | 同左 |
| 既知ASICの型DWORD/配列16への不整合 | 書込み前に拒否（2ケース） | 通過し、検出しない |
| 対象外HVの誤型/HULの誤配列 | 通過し、検出しない（2ケース） | 同左 |
| 固定Commentの誤型 | 拒否 | 同左 |
| EventSlipCount UINT64値17 | 値17を保ちINT64へ自動変更 | 同左 |
| 固定Comment STRING capacity16 | 値を保ち4097へ自動拡張 | 同左 |
| 管理BOR linkの異なるtarget | 固定targetへ再作成 | 同左 |
| 必須ASIC Settings欠損 | 作成せず拒否 | 作成も検出もしない |
| 前方Type欠損＋後方Comment誤型 | Type作成後に拒否（部分変更） | 同左 |

`bash -n`もdeploy1/2で成功。実環境への書込み・デプロイを伴う検証はしていない。

### 不足点と最小限の修正案（未実装）

1. 全体デプロイの固定schema整備と、開発値からの**承認対象だけの新規追加**を別工程にする。
   8181/8081のidentityを確認して必要な設定subtreeをread-only snapshotし、
   `{本番に既存:値を保持、開発のみ:typed valueを追加候補、本番のみ:保持、型/count不一致:拒否}`
   の差分planを先に生成する。parent KEY/leaf競合、STRING capacity、LINKも明示検査する。
   既存Settingsへのdb_pasteや全ODB loadをmergeの代用にしない。
2. 「既存キーは常に保持」を厳密に採用するなら、現在のMANAGED_VALUES上書き、
   link再作成、Runlog型変換、文字列容量拡張を例外として明示承認するか、
   不一致を報告して停止する方針へ分離する。既存本番pathは開発pathに変更しない。
   固定管理値の整備と一般設定mergeを同じ保持保証として説明しない。
3. HV/HUL/拡張ASIC Settingsも型・count検査の対象へ追加する。
   全変更のschema/value前提を**最初の書込み前に**検査し、直前再確認、
   対象別backup/journal、追加キーと既存保護値の読戻しを用意する。
   既存の全体backupは保全し、通常復旧では限定した追加差分だけを戻す。
   HULはODB追加だけでfrontendのbuild/登録/実機取得検証が完了するわけではない。
4. **規則の対象を先に確定する必要がある**。現在の開発HV HardwareAccessEnabled=trueを
   「開発のみだから」とそのまま追加すると、前節の新規HV=false方針と衝突する。
   このキーは自動移行を拒否して個別承認にするか、安全なfalseを明示的な例外とする。
   また、LastApplied、Commands/request ID、Variables/Status/Statistics、Runinfo、Clients、
   Loggerの出力先等を一般の開発値merge対象に含めない。
   これらを除いたdesired Settingsのallowlistを基本とし、値の有効範囲や組合せも検査する。

今回はこのmerge機能、例外方針、拡張schema検査を実装していない。
「既存本番値を全て保持し、新規キーを開発値で移行できる」とは判定できない。

## EASIROC・HUL frontendのODB自動初期化調査（2026-11-05）

**両frontendは不足キーを自動生成する。ただし、全キーがfrontend_init内で作られるわけではなく、
既存の値を全て保持する処理でもない。** 設定/適用記録と、状態/Info/Commonを分けて扱う。
本節は現在の開発ソースの調査結果。実frontendの起動・再起動、実験ODBへの初期化、
実機通信、ASIC Apply/HV操作、deploy scriptの変更・実行は行っていない。
ソース/テストを変更せず、隔離した一時モックのbuild/実行と本README・codex.log（ルート/HUL配下）更新のみ。
開発/本番の既存ODBにも今回アクセスしていない。

### 「HV Settings全13項目」の整理

現行ソースの`/Equipment/EASIROC/Settings/HV`に自動生成する設定は**2項目だけ**。
前節の実ODB比較で開発だけにあったSettings 13 leafは、HV2と拡張Slow Control11の合計だった。
13個のHV設定が定義されているわけではない。以下の11項目はHVの下にも配置されない。

| Settingsからの相対path | 参照初期値 | 自動生成・既存値の扱い |
| --- | --- | --- |
| `HV/CurrentLimit_uA` | DOUBLE[1] 20 µA | 初回HV loopで不足時生成。既存値を保持 |
| `HV/HardwareAccessEnabled` | BOOL[1] false | 初回HV loopで不足時生成。既存trueもfalseへ上書きしない |
| `Probe/ASIC1/Enable` | false | **自動生成なし**。以下11項目の値はC++構造体の既定値で、欠損ODBへ書く初期化ではない |
| `Probe/ASIC1/Channel` | 0 | 同上 |
| `Probe/ASIC1/Signal` | 4 | 同上 |
| `Probe/ASIC2/Enable` | false | 同上 |
| `Probe/ASIC2/Channel` | 0 | 同上 |
| `Probe/ASIC2/Signal` | 4 | 同上 |
| `SelectableLogic/Pattern` | 3 | 同上 |
| `SelectableLogic/OneChannel` | 0 | 同上 |
| `SelectableLogic/HitNumThreshold` | 1 | 同上 |
| `SelectableLogic/AndChannels` | BOOL[64] 全false | 同上 |
| `TriggerWidth` | 118 ns | 同上 |

根拠：`easiroc_hv.h:8`、`easiroc_odb.cxx:1565-1582`、
`easiroc_run_settings.h:106-117,138-164`。
拡張3枝が全てなければlegacy ASIC-onlyとして読める。一枝でもあれば全拡張項目を
型/長さ付きで読もうとし、部分欠損は補充せず失敗する
（`easiroc_odb.cxx:1235-1287,1118-1119`）。
拡張設定を導入する場合は、11項目を完全に揃える明示的な追加工程が必要。

### EASIROC：呼出し順・初期値・保護範囲

`feeasiroc.cxx:899-919`のfrontend_initは、まずinitialize_odbを呼ぶ。
initialize_odbはAcquisition、ASIC、Info、Firmware Readback、LastApplied、Apply/BufferClear
mailboxを初期化し、既存の不確定状態/LastAppliedを読み、runtimeとRunSnapshotを公開する
（同`:240-256`）。失敗はfalseを返しfrontend_initがFE_ERR_ODBで終了する。
その後のSettings読取り・LastApplied読取り失敗も起動失敗となる。

| 自動生成するpath（`/Equipment/EASIROC`基準） | 不足時の初期値 | 既存時 | 型・長さ/失敗時 | 通信・deploy3の必要性 |
| --- | --- | --- | --- | --- |
| `Settings/Enabled`、`Network/IPAddress`、`Acquisition/{ADCEnabled,TDCEnabled,ScalerEnabled}` | true、192.168.10.40、true/true/false。IP STRING capacity64 | 保持 | ensureは存在だけを見る。後段の設定読取り失敗で起動中止 | 初期化helperは通信なし。deploy3は必須既存schemaを要求するため、全体手順で空ODBを自動補完するものではない |
| `Settings/ASICSlowControl/ApplyAtBOR`、`Settings/ASIC1,ASIC2/*` | ApplyAtBOR=false、各ASIC DAC=600、slope=1、HG/LG feedback=100 fF、HG/LG shaping=100/50 ns、InputDAC INT[32]=全480、ChannelEnabled BOOL[32]=全true | 保持 | ensure自体は型/countを検査しない。後段で誤型/32要素不一致ならFE_ERR_ODB | 通信/ASIC書込みなし。前述の必須schema事前用意が必要 |
| `Variables/ASICSlowControl/LastApplied/*` | Valid=false、RequestId=0、ApplyUnixTime=0、ASIC数値/INT[32]=0、ChannelEnabled BOOL[32]=false | 保持。Extended/extrasはここでは生成しない | 誤型/配列長/Extended=trueの不完全snapshotは後段読取りで失敗 | 実適用済みの記録は捏造しない。deploy3は検査/追加対象外。保護の事前検査は別途必要 |
| `Commands/ASICSlowControl/ApplyRequestId`と`Variables/ASICSlowControl`のmailbox | DWORD ID類0、ApplyState=Idle、ApplyInProgress/LastAttemptSucceeded=false、LastApplyError空、LastApplyUnixTime=0 | ensure段階は保持。その後runtime公開でactive/statusは更新 | ID読取り等の失敗は起動中止。古い未処理要求は実行せずackし、不確定状態を記録 | ASIC Applyなし。通常生成だけならdeploy3追加不要 |
| `Commands/BufferClearRequestId`、`Variables/BufferClear/*` | ID/時刻/DrainedBytes=0、State=Idle、InProgress/LastAttemptSucceeded=false、文字列空 | ensure段階は保持。起動時に古い未処理要求を実行せずack | 初期化/ID読取り失敗は起動中止 | buffer clearを起動時に実行しない。通常生成だけならdeploy3追加不要 |
| `Settings/HV/*`、`Commands/HV/*`、`Variables/HV/*` | 下表参照 | Settings/Commandsを保持、Variablesは現在状態へ更新 | **frontend_init後のvoid関数**。失敗時はskip/fault/Unknown等であり、frontend起動自体を取り消さない | 初期値生成だけならdeploy3不要。起動前に抑止/schemaを保証したい場合は事前工程が必要 |
| `Info/Hardware/*`、`Info/Network/*`、`Info/Capabilities/*`、`Info/FrontendBuild` | Model=NIM-EASIROC、ASICCount2、ChannelsPerASIC32、ChannelCount64、TCP24、RBCP4660、readback/scaler能力false、LowGainDecoded=true、build文字列 | **ソース情報で毎起動更新** | db_set失敗なら起動失敗。同じ型の配列長はsetterで更新され得る | 通信なし。deploy3追加不要 |
| `Readback/Firmware/{Valid,Version,SynthesisDate,Raw}` | false、空、空、BYTE[6]ゼロ | **毎起動リセット**。後続診断で更新 | setter失敗は起動中止 | 初期化は通信なし。診断自体は別途通信あり |
| `Variables/Frontend`、各runtime status、`RunSnapshot`等 | 起動時のrun inactive/ready false/現在のSettings等を公開 | **状態公開で更新** | 戻り値をチェックする公開失敗はinitialize_odb失敗 | Settings/LastAppliedと同じ保持保証ではない |

根拠：`easiroc_odb.cxx:66-76,572-815,846-878,893-963,1154-1225`、
`easiroc_run_settings.h:12-29,41-42,48-50`、`feeasiroc.cxx:168-256`。
既存キーを保持するensureと、db_set_valueで公開する状態処理は別。
参照MIDASのsetterは誤型をDB_TYPE_MISMATCHで拒否するが、同型のサイズ変更は
一律禁止ではない（`midas_src/src/odb.cxx:5562-5600,7620-7640`）。
誤型キーを自動的に削除して既定型へ置換するfrontend処理はない。

#### HVの生成タイミングと初回の値

`frontend_init()`はHV runtimeをresetするだけで、HVキーの生成関数は呼ばない。
約1秒周期のfrontend_loopから`process_hv_requests()`を呼ぶ
（`feeasiroc.cxx:900-901,1320-1354`）。
ローカルODB lockの取得とIP/registry列挙ができた装置baseごとに不足キーを作成する。
IPキーが空ODBにまだなければrouteを作れず、HVだけを先に空ODBへ生成するわけではない。
追加Devicesは登録済みentryだけを扱い、未知装置のSettings/ASIC一式を自動登録する処理ではない。

| HVのpath | 初回/生成時の値・扱い |
| --- | --- |
| `Settings/HV/CurrentLimit_uA` | DOUBLE 20。既存10/12.5等はそのまま。20は`kDefaultCurrentLimitUa` |
| `Settings/HV/HardwareAccessEnabled` | BOOL false。既存trueは保持し、後続tickで条件が揃えば測定するため、起動だけで常に通信抑止になるとは限らない |
| `Commands/HV/TargetVoltage_V`、`Enable` | DOUBLE 0、BOOL false。不足時だけ生成 |
| `Commands/HV/Operation`、`RequestPayload` | 空STRING。不足時だけ生成（コードの初期bufferは1 byte） |
| `Commands/HV/RequestId`、`Variables/HV/LastHandledRequestId` | DWORD 0で不足時生成。古いrequest>handledは実行せずIndeterminateとして処理し、handledを更新 |
| `Variables/HV`の状態 | EnabledState/SetVoltageState/RampStatus=Unknown、測定値0、MeasurementValid=false、時刻0、InProgress=false、Result=Idle。能力/BlockedReason/heartbeat等はその場の前提条件から公開 |

既存の測定値/OFF状態を成功測定や実機読戻しとして復元しない。
HV schema作成/RequestId読取り失敗ではその装置の処理をcontinueする。
CurrentLimitの誤型/長さ/範囲異常はlimit_ok=false、HardwareAccessEnabled読取り失敗はfalse扱い。
状態書込み失敗はfaultとbest-effort Unknown/Invalidで扱うが、戻り値はvoidでfrontendは継続する。
HV操作の許可は個別判定であり、CurrentLimit無効でもEmergencyの扱いは別である。
根拠：`easiroc_odb.cxx:1326-1331,1529-1587,1590-1597,1602-1637,1677-1723`。

**EASIROC起動は通信なしではない。** Enabled=trueが既定なので、frontend_init成功後に
RBCP firmware読取りとTCP24の接続診断を開始する（`feeasiroc.cxx:920-932`）。
HVの新規falseはMADC/DAC通信の抑止であって、通常診断まで停止する設定ではない。
ASIC設定の既定値作成はASIC Applyを意味しない。HV計測もMADC register書込みを伴うため、
既存HardwareAccessEnabled=trueを保持した起動/loopを読取り専用の初期化試験とは扱わない。

### HUL Scaler：起動時の自動生成

| 自動生成するpath（`/Equipment/HUL Scaler 1`基準） | 初期値 | 既存値の扱い/不一致 | 通信・deploy3の必要性 |
| --- | --- | --- | --- |
| `Settings/IPAddress` | STRING capacity64、192.168.10.60 | db_get_value(create=true)で不足時作成。既存IPv4を読む。誤型/読取り失敗・不正IPはfalse | 初期化helperは通信なし。生成をfehulに任せるならdeploy3不要だが、全体手順はfehulを起動しない |
| `Settings/RBCPPort` | DWORD 4660 | 既存保持。0/>65535・誤型等は失敗 | 同上 |
| `Settings/TCPPort` | DWORD 24 | 既存保持。0/>65535・誤型等は失敗 | 同上 |
| `Settings/EnableBlock` | DWORD 3 | 既存保持。ただし3以外を設定検査で拒否 | 同上 |
| `Settings/FreshnessMs` | DWORD 2000 | 既存保持。0は拒否 | 同上 |
| `Settings/DrainMs` | DWORD 2000 | 既存保持。200未満/10000超は拒否 | 同上 |
| `Variables/HCUM`、`HDIF`、`HVAL` | DWORD[64]、DWORD[64]、DWORD[15]の全0 | **既存型/countが一致すれば値を保持**。不一致はensure_variable=false | 生成/検査のみ。MFEのRO_ODBでRUN中の周期bankを反映 |
| `Status/Connection`、`Status/Gate` | Unknown | **毎起動更新**。status_stringは書込み失敗をログするだけで、これ単独では起動を失敗させない | frontend_initには通信なし。loopで状態更新 |
| `Common`、`Statistics` | Equipment ID10、SYSTEM、EQ_PERIODIC、Enabled=true、RO_RUNNING|RO_ODB、Period1000 ms、Log history1 | MFEが登録/record検査。common_overwrite=falseで既存Commonを読むが、host/client/status等は更新。Statisticsはruntime管理 | MFE登録で作るので一般生成だけならdeploy3不要。既存ID/Enabled等を事前検査すべき |

6 Settingsは`fehul.cxx:38-45,123-147`、Variablesは同`:150-162`。
frontend_initはload_settings(true)、Variables3本、STARTABORT登録の各失敗でFE_ERR_ODBを返す
（同`:362-374`）。MFEはfrontend_initの非SUCCESSでdisconnectして終了する。
不足キー生成はtransactionではなく、途中エラーまでに作ったキーは自動削除しない。

`TriggerSelect`も`SelectTrigger`も現行SettingsのODBキーではない。
BORでTRM::kAddrSelectTriggerへkRegL1Ext(1)を固定設定し、NIM IN1等も設定/確認する
（`fehul.cxx:427-443`）。設定6項目の初期化とBORの実機設定を混同しない。
周期event ID10、最終EOR event ID11、Bank HCUM/HDIF/HVAL。
ID11は手動送信で、別の最終用Equipment/Commonを起動時に作らない
（同`:288-334`）。初期化だけでScalerイベントを受信・保存したとは言えない。

**HULも通常起動後は通信し得る。** frontend_init自体はsocket/reset/Gate操作をしないが、
MFEが作る新規Common/Enabledはtrueである。
次のfrontend_loopから有効・遷移なしならRBCP読取り監視が始まる（2秒周期、最初のloopでも送信し得る）。
Enable OFFでは送信しない。TCP接続/reset/Gate操作はBOR側
（`fehul.cxx:547-572`、`hul_monitor.h:17-48`）。
従って実機通信禁止中に「キーを作るだけ」の目的で通常fehulを起動してはならない。

### MFEによる登録とdeploy3との関係

参照MIDASはregister_equipment→frontend_initの順で呼び、Common/Variables/Statisticsを
先に登録する（`midas_src/src/mfe.cxx:351-457,501-572,2618-2632`）。
EASIROCのequipment_common_overwriteはtrueで、既存Commonの多くをコード値へ更新する。
MFEはEnabled/Hidden/Event limit等を例外保持する。HULはfalseで既存Commonを読んで使う。
両方ともfrontend名/host/status等のruntime欄は更新する
（`feeasiroc.cxx:871-891`、`fehul.cxx:324-334`、`mfe.cxx:421-457,473-489`）。
Common構造不整合ではMFEがrecordの修復/再作成を試みる経路もある
（mfe.cxx:383-396）。Settingsの型不一致拒否をCommon全体へ一般化しない。
同じ呼出し順と該当分岐は本番MIDAS参照ソースでも確認したが、実MFEの初期化試験はしていない。

既存deploy3にHV/拡張ASIC/LastApplied/HULの自動生成はなく、
EASIROC基本Settings22 leafは**frontend起動前**に存在/型/countを要求する
（`deploy3_configure_live_odb.py:369-381,473-476`）。
この22項目preflightは開発版deploy3の処理で、現本番配置版にはまだない（前節の版比較参照）。
そのため、開発版を取り込む手順で空ODBに対して「後でfrontendが作るから全体deployをそのまま実行できる」とはならない。
新規22項目は別途事前用意が必要。既存22項目が揃いlegacyを維持する場合、
HV2の生成だけのためのdeploy3追加は必須ではないが、安全な抑止・schema事前検査は有用。
拡張11項目を使うなら、完全なschema追加工程が必要である。

またdeploy2は元々動いていたfeeasirocだけを再起動し、元々不在なら起動しない。
HULはbuild/起動/復元対象外（`deploy2_impl.sh:205-222,235-242`）。
従って既存本番反映方式で両frontendの自動初期化が必ず実行されるわけではない。
初期値の由来も開発ODBではなくC++コード/Equipment定義である。

### 今回確認した範囲・未確認事項

一時directoryを開発build配下に作り、実easiroc_odb.cxxをsection単位で再コンパイルし、
既存のメモリ内MIDAS mock APIへリンクした。HULのget_or_create/load_settings/ensure_variable/
status_stringは実ソースから抽出し、通信オブジェクトを持たない最小moduleへ組み込んだ。
既存mockを一時コピーし、create=trueと型拒否の挙動を参照MIDASに合わせた。
MIDAS library/mfe/frontend main/transportはリンクせず、実験接続・socket・共有メモリは使用していない。
一時ファイルは試験終了時に削除した。

**41 checks成功（EASIROC側27＋HUL側14）**。コンパイラ警告・エラーなし。
空mock/欠損の補充、既存ASIC値・InputDAC/LastApplied保持、ASICの誤型/16要素拒否、
拡張部分欠損、作成権限エラー、HV20/false生成と既存12.5/true保持、HV誤型の非修復、
HV作成失敗時の継続、Info/Readbackの上書き、HUL6既定値/部分欠損/既存値保持、
HUL誤型/誤配列/範囲異常、Variables型/長さ/既存カウンタ保持と作成失敗、Status上書きを確認。
HV backendは呼ばれたら失敗するcallbackを注入し、**hardware callback呼出し0**を確認した。
模擬ASIC Apply、模擬HV昇降圧の操作経路も今回実行していない。

これは初期化helper/reader/HV serviceの隔離試験であり、実frontend_init全体やMFE登録、
実共有メモリODBでの権限・型修復、装置通信、本番稼働を確認した試験ではない。
既存本番ライブラリでの実起動、前提を満たしたHV監視、HUL起動後監視はソース確認のみ。
実機・既存ODBへの初期化で未確認部分を埋める操作は行っていない。
