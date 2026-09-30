# Live deployment

通常の更新手順は次のとおりです。

1. `nagao` の `/home/nagao/midas/midas/online` で開発・testを行う。
2. 変更をcommitし、`origin/main`へpushする。
3. 実DAQホスト `nexdaq1` に `daq` ユーザーでログインする。
4. `/home/daq/midas/midas/online` へ移動し、次の1コマンドを実行する。

   ```sh
   ./scripts/deploy/deploy1_init.sh
   ```

RunはSTOPPEDで、作業中にRun Startを行わない保守時間に実行してください。完了表示と
`deploy4` のread-only smoke test成功を確認してから運用に戻します。

## 初回導入の前提

既存のlive checkoutにはこの4ファイルがまだ存在しません。**最初の1回だけ**、
`daq` の実ホストシェルで追跡対象に未commit変更がないことを確認し、
`git pull --ff-only origin main` でbootstrapを取得してください。その後の更新は
上の1コマンドです。この初回取得もlive変更なので、開発環境から実行しません。

`/home/daq/midas/midas/exptab`、MIDAS、ROOT、ROOTANA、既存の4つのsystemd unit、
8081番ポートのmhttpd、Runlogディレクトリが必要です。`daq` がパスワード入力なしで
`midas-daq-monitor.service` と `midas-analyzer.service` を `systemctl stop/start`
できる限定権限も必要です。unitのinstall/updateはこの初版では行いません。
追跡中のunitと設置済みunitが異なる場合は、プロセスを停止する前に中断します。
ホスト名は安全確認として `nexdaq1` に固定しています。ホストを交換・改名した場合は
実機構成を確認してGit上でこの条件を更新してください。

## 番号順の処理

- `deploy1_init.sh`: `daq`・host・checkout・Git状態・STOPPED・遷移なしを確認し、
  元のfrontend/service状態を記録します。排他ロックを保持し、自分自身を一時コピー
  してから `git pull --ff-only origin main` を実行し、更新後のstep 2を呼びます。
  未追跡ファイルは削除しません。Gitが未追跡ファイルとの衝突を検出したら停止します。
- `deploy2_impl.sh`: ODBを一意のファイルへbackupし、再開用journalを保存します。
  必要なときだけ稼働中のfrontendとmonitor/analyzerを停止してproduction build・
  unit testを実施します。step 3、Runlog用の欠けた派生ファイルの準備、元々稼働
  していたプロセスだけの復帰、step 4を順に行います。同一commit・同一binaryで
  完了済みならプロセスを再起動しません。
- `deploy3_configure_live_odb.py`: 固定schemaのキー・型・文字列長、JSON Runlogの
  BOR/EOR linkのリンク先と順序、ProgramsとCustomの管理対象だけを収束させます。
  ODB全体のloadはしません。予期しないlink名・型・本番パスなら停止します。
- `deploy4_check_live.py`: Git commit、binary、固定ODB schema、本番パス、Programs、
  Custom、Runlog/ELOG、service/frontend状態、8081番ポートのページを読み取り専用
  で確認します。Run Startやhardware testはしません。

## 失敗・再実行

失敗したらRun Startをせず、表示された失敗箇所を調べてください。ODB backupは
`/home/daq/midas/midas/backups/odb-before-deploy-*.odb`、再開用journalは
`/home/daq/midas/midas/deploy-state.json` です。buildや切替途中で失敗した場合は
journalが元の稼働状態を保持します。原因を直して `deploy1_init.sh` を再実行すると、
元々起動していたfrontendだけを復帰します。journalを手作業で消さないでください。
backupを自動で丸ごとODBへloadする処理はありません。

STOPPED確認は各変更段階で再実行します。標準MIDASのRun Start全経路をこの
スクリプトだけで原子的に禁止する仕組みはありません。保守中のRun Startを
運用上禁止してください。開始との競合を検出した場合、処理は停止します。

## 保持するlive値

Run Number、Run ParametersのType/Comment/ExperimentLabel、Run Elogの
Enabled・Last Run・Last Attempt Run・Last Status・Last Error、DAQ実測値、
機器設定、AnalyzerのHistogram/Page設定、既存DAQ data・Runlog・ELOG、既存の
`runlog_selection.json`、無関係なPrograms/Custom設定は上書きしません。
新規に欠けた履歴キーを作る場合の初期値は0または空文字であり、既存履歴は変更しません。
`/Custom/Path` と Run Elog Web Portはlive値を検証し、不一致なら停止します。
`scripts/archive_midas_data.sh` はlive固有の未追跡ファイルとして保持します。
`git reset --hard` と `git clean` は使用しません。

## 個別診断

実ホスト上で、RunがSTOPPEDであることを確認してから行います。

```sh
cd /home/daq/midas/midas/online
python3 scripts/deploy/deploy4_check_live.py   # 読み取り専用
python3 scripts/deploy/deploy3_configure_live_odb.py  # 固定schemaを変更し得る
```

step 2はbootstrapのロックと元の稼働状態を必要とするため、単独実行しません。
静的確認には `bash -n scripts/deploy/deploy{1,2}_*.sh` と
`python3 -m py_compile scripts/deploy/deploy{3,4}_*.py` を使えます。
