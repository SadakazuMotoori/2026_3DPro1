#pragma once

#include "Framework/GameObject/KdGameObject.h"
#include "Framework/Direct3D/Polygon/KdSquarePolygon.h"

class DistortionObject : public KdGameObject
{
public:
	// このオブジェクトが出す歪みの種類。
	// 将来的に用途が増えても、ここへ種類を足していけば同じ描画経路へ載せられる。
	enum class Type
	{
		HeatHaze,	// 蜃気楼や熱気のような、面の中でゆらぐ歪み
		ShockWave,	// 中心から外側へ広がる輪状の歪み
	};

	DistortionObject() {}
	~DistortionObject() override {}

	// 歪み用板ポリの初期設定を行う。
	void Init() override;
	// 毎フレームの進行処理を行う。
	void Update() override;
	// 歪み専用パスへ描画し、背景をどの方向へ曲げるかを書き出す。
	void DrawDistortion() override;

	// 外部から見た目を調整するための設定関数群。
	// A/B や炎、HIT エフェクト側から必要な値だけを流し込めるようにしている。
	void SetEffectType(Type type) { m_type = type; }
	void SetSize(const Math::Vector2& size) { m_size = size; }
	void SetStrength(float strength) { m_strength = strength; }
	void SetNoiseTiling(const Math::Vector2& tiling) { m_noiseTiling = tiling; }
	void SetScrollSpeed(const Math::Vector2& speed) { m_scrollSpeed = speed; }

	// 衝撃波用の進行パラメータ設定。
	// radiusSpeed : 1 フレームごとに半径をどれだけ広げるか
	// maxRadius   : 板ポリ内の UV 空間で許可する最大半径
	// thickness   : 輪の太さ
	// loop        : true なら最大半径到達後に 0 へ戻して再生し続ける
	void SetShockWave(float radiusSpeed, float maxRadius, float thickness, bool loop = true)
	{
		m_ringSpeed = radiusSpeed;
		m_ringMaxRadius = maxRadius;
		m_ringThickness = thickness;
		m_loop = loop;
	}

private:
	// カメラへ常に正対するワールド行列を作る。
	// 歪みの発生位置は 3D 空間に置きたいので、ここでは板ポリをビルボード化する。
	Math::Matrix CreateBillboardMatrix() const;

	// 現在の設定値を歪み用シェーダーの定数バッファへ反映する。
	void ApplyDistortionParameter() const;

	Type m_type = Type::HeatHaze;

	// 歪み発生範囲を表す板ポリ。
	// 実際の色ではなく、最終段で使う「歪み情報」を描くために使う。
	KdSquarePolygon m_quad;

	// 共通パラメータ
	Math::Vector2 m_size = { 2.0f, 2.0f };
	float m_strength = 0.02f;
	float m_time = 0.0f;

	// 熱気・蜃気楼用パラメータ
	Math::Vector2 m_noiseTiling = { 2.0f, 2.0f };
	Math::Vector2 m_scrollSpeed = { 0.0f, -0.25f };

	// 衝撃波用パラメータ
	float m_ringRadius = 0.0f;
	float m_ringSpeed = 0.02f;
	float m_ringMaxRadius = 0.9f;
	float m_ringThickness = 0.12f;
	bool m_loop = true;
};
