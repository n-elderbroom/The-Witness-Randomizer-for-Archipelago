#include "APRandomizer.h"

bool APRandomizer::Connect(HWND& messageBoxHandle, std::string& server, std::string& user, std::string& password) {
	std::string uri = buildUri(server);

	ap = new APClient("uuid", "The Witness", uri);

	try {	ap->set_data_package_from_file(DATAPACKAGE_CACHE);	}
	catch (std::exception) { /* ignore */ }

	bool connected = false;
	bool hasConnectionResult = false;

	ap->set_room_info_handler([&]() {
		const int item_handling_flags_all = 7;

		ap->ConnectSlot(user, password, item_handling_flags_all, {}, {0, 3, 2});
	});

	ap->set_location_checked_handler([&](const std::list<int64_t>& locations) {
		while (!randomizationFinished) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		for (const auto& locationId : locations)
			async->MarkLocationChecked(locationId);
	});

	ap->set_items_received_handler([&](const std::list<APClient::NetworkItem>& items) {
		while (!randomizationFinished) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		std::vector<int> receivedItems;
		std::map<int, int> counts;

		for(auto item : items){
			if (mostRecentItemId >= item.index + 1) continue;

			mostRecentItemId = item.index + 1;

			int i = item.item;

			if (counts.find(i) == counts.end()) {
				counts[i] = 1;
				receivedItems.emplace_back(i);
			}

			else {
				counts[i]++;
			}
		}

		for (int itemId : receivedItems) {
			std::string count = "";

			if (counts[itemId] > 1) {
				count = " (x" + std::to_string(counts[itemId]) + ")";
			}

			async->queueMessage("Received " + ap->get_item_name(itemId) + count + ".");
		}

		_memory->WritePanelData<int>(0x00293, BACKGROUND_REGION_COLOR + 12, { mostRecentItemId });
		
		for (const auto& item : items) {
			unlockItem(item.item);

			if (item.item < ITEM_TEMP_SPEED_BOOST)
				panelLocker->UpdatePuzzleLocks(state, item.item);

			if (itemIdToDoorSet.count(item.item)) {
				for (int doorHex : itemIdToDoorSet[item.item]) {
					async->UnlockDoor(doorHex);
				}
			}
		}
	});

	ap->set_slot_connected_handler([&](const nlohmann::json& slotData) {
		Seed = slotData["seed"];
		FinalPanel = slotData["victory_location"];

		Hard = slotData.contains("hard_mode") ? slotData["hard_mode"] == true : false;
		UnlockSymbols = slotData.contains("shuffle_symbols") ? slotData["shuffle_symbols"] == true : true;
		EarlyUTM = slotData.contains("early_secret_area") ? slotData["early_secret_area"] == true : false;
		if (slotData.contains("mountain_lasers")) MountainLasers = slotData["mountain_lasers"];
		if (slotData.contains("challenge_lasers")) ChallengeLasers = slotData["challenge_lasers"];
		DisableNonRandomizedPuzzles = slotData.contains("disable_non_randomized_puzzles") ? slotData["disable_non_randomized_puzzles"] == true : true;

		for (auto& [key, val] : slotData["panelhex_to_id"].items()) {
			int panelId = std::stoul(key, nullptr, 16);
			int locationId = val;

			panelIdToLocationId.insert({ panelId, locationId });
		}

		if (slotData.contains("item_id_to_door_hexes")) {
			for (auto& [key, val] : slotData["item_id_to_door_hexes"].items()) {
				int itemId = std::stoul(key, nullptr, 10);
				std::set<int> v = val;

				itemIdToDoorSet.insert({ itemId, v });
			}
		}

		if (slotData.contains("door_hexes")) {
			for (int val : slotData["door_hexes"]) {
				allDoors.insert(val);
			}
		}

		mostRecentItemId = _memory->ReadPanelData<int>(0x00293, BACKGROUND_REGION_COLOR + 12);

		connected = true;
		hasConnectionResult = true;
	});

	ap->set_slot_refused_handler([&](const std::list<std::string>& errors) {
		auto errorString = std::accumulate(errors.begin(), errors.end(), std::string(),
			[](const std::string& a, const std::string& b) -> std::string {
				return a + (a.length() > 0 ? "," : "") + b;
			});

		std::wstring wError = std::wstring(errorString.begin(), errorString.end());

		connected = false;
		hasConnectionResult = true;

		WCHAR errorMessage[200] = L"Connection Failed: ";
		wcscat_s(errorMessage, 200, wError.data());

		MessageBox(messageBoxHandle, errorMessage, NULL, MB_OK);
	});

	ap->set_print_json_handler([&](const std::list<APClient::TextNode>& msg, const APClient::NetworkItem* networkItem, const int* receivingPlayer) {
		if (!receivingPlayer || !networkItem || networkItem->player != ap->get_player_number())
			return;

		const APClient::NetworkItem item = *networkItem;
		const int receiver = *receivingPlayer;

		auto findResult = std::find_if(std::begin(panelIdToLocationId), std::end(panelIdToLocationId), [&](const std::pair<int, int>& pair) {
			return pair.second == item.location;
		});

		if (findResult == std::end(panelIdToLocationId)) return;

		bool receiving = receiver == ap->get_player_number();

		std::string player = ap->get_player_alias(receiver);
		std::string itemName = ap->get_item_name(item.item);

		if (!receiving) {
			async->queueMessage("Sent " + itemName + " to " + player + ".");
		}
		
		panelLocker->SetItemReward(findResult->first, item);
	});

	ap->set_data_package_changed_handler([&](const nlohmann::json& data) {
		ap->save_data_package(DATAPACKAGE_CACHE);
	});

	(new APServerPoller(ap))->start();

	auto start = std::chrono::system_clock::now();

	while (!hasConnectionResult) {
		if (DateTime::since(start).count() > 5000) { //5 seconnd timeout on waiting for response from server
			connected = false;
			hasConnectionResult = true;

			std::wstring wideServer = Converty::Utf8ToWide(uri);

			WCHAR errorMessage[200] = L"Timeout while connecting to server: ";
			wcscat_s(errorMessage, 200, wideServer.data());

			MessageBox(messageBoxHandle, errorMessage, NULL, MB_OK);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	return connected;
}

void APRandomizer::unlockItem(int item) {
	switch (item) {
		case ITEM_DOTS:									state.unlockedDots = true;							break;
		case ITEM_COLORED_DOTS:							state.unlockedColoredDots = true;				break;
		case ITEM_SOUND_DOTS:							state.unlockedSoundDots = true;					break;
		case ITEM_SYMMETRY:								state.unlockedSymmetry = true;					break;
		case ITEM_TRIANGLES:								state.unlockedTriangles = true;					break;
		case ITEM_ERASOR:									state.unlockedErasers = true;						break;
		case ITEM_TETRIS:									state.unlockedTetris = true;						break;
		case ITEM_TETRIS_ROTATED:						state.unlockedTetrisRotated = true;				break;
		case ITEM_TETRIS_NEGATIVE:						state.unlockedTetrisNegative = true;			break;
		case ITEM_STARS:									state.unlockedStars = true;						break;
		case ITEM_STARS_WITH_OTHER_SYMBOL:			state.unlockedStarsWithOtherSimbol = true;	break;
		case ITEM_B_W_SQUARES:							state.unlockedStones = true;						break;
		case ITEM_COLORED_SQUARES:						state.unlockedColoredStones = true;				break;
		case ITEM_SQUARES: state.unlockedStones = state.unlockedColoredStones = true;				break;

		//Powerups
		case ITEM_TEMP_SPEED_BOOST:					async->ApplyTemporarySpeedBoost();				break;
		case ITEM_PUZZLE_SKIP:						async->AddPuzzleSkip(); break;

		//Traps
		case ITEM_POWER_SURGE:							async->TriggerPowerSurge();						break;
		case ITEM_TEMP_SPEED_REDUCTION:				async->ApplyTemporarySlow();						break;
	}
}

std::string APRandomizer::buildUri(std::string& server)
{
	std::string uri = server;

	if (uri.rfind("ws://", 0) == std::string::npos)
		uri = "ws://" + uri;
	if (uri.find(":") == std::string::npos)
		uri = uri + ":38281";
	else if (uri.rfind(":") == uri.length())
		uri = uri + "38281";

	return uri;
}

void APRandomizer::PostGeneration(HWND loadingHandle, HWND skipButton, HWND availableSkips) {

	PreventSnipes(); //Prevents Snipes to preserve progression randomizer experience

	if(MountainLasers != 7 || ChallengeLasers != 11) Special::SetRequiredLasers(MountainLasers, ChallengeLasers);

	async = new APWatchdog(ap, panelIdToLocationId, FinalPanel, panelLocker, skipButton, availableSkips);
	async->SkipPreviouslySkippedPuzzles();
	
	SeverDoors();

	if (UnlockSymbols)
		setPuzzleLocks(loadingHandle);

	async->ResetPowerSurge();

	randomizationFinished = true;
	_memory->showMsg = false;

	async->start();
}

void APRandomizer::setPuzzleLocks(HWND loadingHandle) {
	for (int i = 0; i < sizeof(AllPuzzles) / sizeof(AllPuzzles[0]); i++)	{
		std::wstring text = L"Locking puzzles: " + std::to_wstring(i) + L"/" + std::to_wstring(sizeof(AllPuzzles));
		SetWindowText(loadingHandle, text.c_str());

		if (!_memory->ReadPanelData<int>(AllPuzzles[i], SOLVED));
			panelLocker->UpdatePuzzleLock(state, AllPuzzles[i]);
	}

	SetWindowText(loadingHandle, L"Done!");
}

void APRandomizer::GenerateNormal() {
	if (DisableNonRandomizedPuzzles)
		panelLocker->DisableNonRandomizedPuzzles();

	if (EarlyUTM)
		MakeEarlyUTM();
}

void APRandomizer::GenerateHard() {
	if (DisableNonRandomizedPuzzles)
		panelLocker->DisableNonRandomizedPuzzles();

	if (EarlyUTM)
		MakeEarlyUTM();
}

void APRandomizer::PreventSnipes()
{
	// Distance-gate shadows laser to prevent sniping through the bars
	_memory->WritePanelData<float>(0x19650, MAX_BROADCAST_DISTANCE, { 2.7 });
}

void APRandomizer::MakeEarlyUTM()
{
	_memory->OpenDoor(0x2D73F);
}

void APRandomizer::SkipPuzzle() {
	async->SkipPuzzle();
}

void APRandomizer::SeverDoors() {
	for (int id : allDoors) {
		async->SeverDoor(id);
	}
}