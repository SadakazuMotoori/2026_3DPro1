#pragma once

class CameraBase;

// 太陽方向を基準に、画面上へレンズフレアを重ね描きするオブジェクト。
class LensFlare : public KdGameObject
{
public:
	LensFlare()						{}
	~LensFlare()		override	{}

	// フレア用テクスチャを読み込む。
	void Init()			override;
	// カメラ位置とライト方向から、フレアの表示位置と明るさを毎フレーム更新する。
	void PostUpdate()	override;
	// 通常パス側のフレア本体とゴーストを描画する。
	void DrawSprite()	override;

	// フレア計算の基準になるカメラを設定する。
	void SetCamera(const std::shared_ptr<CameraBase>& camera);

private:
	// カメラから光源方向へレイを飛ばし、遮蔽物の有無から表示率を返す。
	float CalcOcclusionRate(const Math::Vector3& camPos, const Math::Vector3& sunWorldPos, const CameraBase* camera) const;
	// スプライトシートから各パーツを配置し、フレア全体を描画する。
	void DrawFlare();
	// スプライトシートから指定セルの矩形を取得する。
	Math::Rectangle GetSrcRect(int xIndex, int yIndex) const;

	// フレア計算に使うカメラ参照。寿命は外側で管理するため weak_ptr で保持する。
	std::weak_ptr<CameraBase>	m_wpCamera;
	// フレア描画に使うスプライトシート。
	std::shared_ptr<KdTexture>	m_spTexture = nullptr;

	// 光源をスクリーンへ投影した座標。
	Math::Vector3				m_sunScreenPos = Math::Vector3::Zero;
	// 遮蔽と画面端フェードを加味した現在の描画強度。
	float						m_intensity = 0.0f;
};
