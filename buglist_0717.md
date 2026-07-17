・オフラインレンダーでDSPが突然バイパスされる可能性がある。
QuanpressorDSP::prepare() でスクラッチバッファを最大 65536 samples に固定しているため
DAWのオフラインレンダーや特殊なホストが65536サンプルを超えるブロックを渡した場合、そのブロックだけ入力信号が未処理のまま通る。音切れではなく、突然コンプレッションが外れる形になるので発見しにくい。

・FET Ratioの状態がEditorの有無で変わる。例えばホストオートメーションが5.9を設定すると、
    Editorが閉じている：パラメータ値は5.9、実音は4:1
　 Editorを開く：パラメータが4.0へ書き換えられる
　 ホストにも変更通知が送られる
　という挙動になる。Editorを開いたことがセッション状態を書き換えるのは避けたい

・方式切替時に150ms間DSP負荷がほぼ倍になる。150msは長いかも。

・GR Matchがホストパラメータをバックグラウンドで動かすときに、Thresholdがユーザーまたはホストから変更されたことを検知したら、GR Matchを解除するように。

・フィルター係数を毎ブロック設定している。DetectorとGain Cellの記憶がすべて消える。

・方式切替レベル補正が素材依存で永続化している。

・ステレオリンクONでエンジンをリセットしている

・OPTOのRatio 8:0で明らかに挙動がおかしくなる

・previousAutoEnabledが使われていない。Auto Level切替時の初期化などに使用していた残骸と思われる

・メーター計算で入力と出力をそれぞれ二重走査している。RMS計算のために再び全サンプルを走査している。ピークと二乗和は1ループで同時に求められるのでは。
float peak = 0.0f;
double energy = 0.0;
for (int i = 0; i < samples; ++i)
{
    const float sample = data[i];
    peak = juce::jmax(peak, std::abs(sample));
    energy += static_cast<double>(sample) * sample;
}

・パラメータ名が内部仕様と一致していない
"High Preserve Frequency"
"High Preserve Amount"
"Low Preserve Frequency"
"Low Preserve Amount"
