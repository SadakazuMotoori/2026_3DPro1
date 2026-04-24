#include "GameScene.h"
#include"../SceneManager.h"

#include "../../GameObject/Camera/FPSCamera/FPSCamera.h"
#include "../../GameObject/Camera/TPSCamera/TPSCamera.h"

#include "../../GameObject/Map/Stage01/Stage01.h"
#include "../../GameObject/Character/Player/SkinMeshMan/SkinMeshMan.h"
#include "../../GameObject/Character/Enemy/Enemy01/Enemy01.h"
#include "../../GameObject/Effect/DistortionObject/DistortionObject.h"

void GameScene::Event()
{
	if (GetAsyncKeyState('T') & 0x8000)
	{
		SceneManager::Instance().SetNextScene
		(
			SceneManager::SceneType::Title
		);
	}
}

void GameScene::Init()
{
	//===================================================================
	// ステージ初期化
	//===================================================================
	std::shared_ptr<Stage01> _stage = std::make_shared<Stage01>();
	_stage->Init();
	AddObject(_stage);

	//===================================================================
	// キャラクター初期化
	//===================================================================
	std::shared_ptr<SkinmeshMan> _skinmeshMan = std::make_shared<SkinmeshMan>();
	_skinmeshMan->Init();
	_skinmeshMan->RegistHitObject(_stage);
	AddObject(_skinmeshMan);

	std::shared_ptr<Enemy01> _enemy01 = std::make_shared<Enemy01>();
	_enemy01->Init();
	_enemy01->RegistHitObject(_stage);
	AddObject(_enemy01);
	_skinmeshMan->RegistHitObject(_enemy01);

	//===================================================================
	// 背景歪みサンプル初期化
	//===================================================================
	// まずは描画経路の確認が目的なので、
	// ワールドへ 2 種類のサンプルを直接置いて効果の見え方を確認しやすくする
	// 本来はここを A/B、炎、HIT エフェクトなどへ差し替えていく想定
	std::shared_ptr<DistortionObject> _heatHaze = std::make_shared<DistortionObject>();
	_heatHaze->Init();
	_heatHaze->SetEffectType(DistortionObject::Type::HeatHaze);
	_heatHaze->SetPos({ 2.5f, 1.8f, 5.0f });
	_heatHaze->SetSize({ 3.0f, 4.5f });
	_heatHaze->SetStrength(0.012f);
	_heatHaze->SetNoiseTiling({ 1.5f, 3.0f });
	_heatHaze->SetScrollSpeed({ 0.03f, -0.35f });
	AddObject(_heatHaze);

	std::shared_ptr<DistortionObject> _shockWave = std::make_shared<DistortionObject>();
	_shockWave->Init();
	_shockWave->SetEffectType(DistortionObject::Type::ShockWave);
	_shockWave->SetPos({ 0.0f, 1.2f, 2.0f });
	_shockWave->SetSize({ 4.5f, 4.5f });
	_shockWave->SetStrength(0.035f);
	// 半径を毎フレーム少しずつ広げる事で、
	// 板ポリ 1 枚でも「中心から外へ広がる輪」に見せている
	_shockWave->SetShockWave(0.018f, 0.9f, 0.12f, true);
	AddObject(_shockWave);

	//===================================================================
	// カメラ初期化
	//===================================================================
	std::shared_ptr<TPSCamera> _camera = std::make_shared<TPSCamera>();
	_camera->Init();
	_camera->SetTarget(_skinmeshMan);
	_camera->RegistHitObject(_stage);

	// キャラクターにカメラ情報をセット
	_skinmeshMan->SetCamera(_camera);
	AddObject(_camera);
}
