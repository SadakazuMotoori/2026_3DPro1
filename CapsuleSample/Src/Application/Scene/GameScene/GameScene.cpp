#include "GameScene.h"
#include"../SceneManager.h"

#include "../../GameObject/Camera/FPSCamera/FPSCamera.h"
#include "../../GameObject/Camera/TPSCamera/TPSCamera.h"

#include "../../GameObject/Map/Stage01/Stage01.h"
#include "../../GameObject/Map/Stage01/Mirror/Mirror.h"
#include "../../GameObject/Character/Player/SkinMeshMan/SkinMeshMan.h"
#include "../../GameObject/Character/Enemy/Enemy01/Enemy01.h"
#include "../../GameObject/Effect/Decal.h"
#include "../../GameObject/Effect/LensFlare/LensFlare.h"

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

	std::shared_ptr<Mirror> _mirror = std::make_shared<Mirror>();
	_mirror->Init();
	AddObject(_mirror);

	std::shared_ptr<Decal> _decal = std::make_shared<Decal>();
	_decal->Init();
	_decal->SetScale({ 2.2f, 0.35f, 2.2f });
	_decal->SetPos({ 1.0f, 0.05f, 2.5f });
	_decal->SetColor({ 0.55f, 0.08f, 0.08f, 0.78f });
	AddObject(_decal);
	
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
	// カメラ初期化
	//===================================================================
	std::shared_ptr<TPSCamera> _camera = std::make_shared<TPSCamera>();
	_camera->Init();
	_camera->SetTarget(_skinmeshMan);
	_camera->RegistHitObject(_stage);

	// キャラクターにカメラ情報をセット
	_skinmeshMan->SetCamera(_camera);
	AddObject(_camera);

	std::shared_ptr<LensFlare> _lensFlare = std::make_shared<LensFlare>();
	_lensFlare->Init();
	_lensFlare->SetCamera(_camera);
	AddObject(_lensFlare);

	_decal->SetTarget(_skinmeshMan);
}
