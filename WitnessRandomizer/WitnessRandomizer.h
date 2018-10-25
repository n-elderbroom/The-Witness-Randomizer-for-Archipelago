#pragma once

int SWAP_TARGETS = 0x1;
int SWAP_LINES = 0x2;
int SWAP_STYLE = 0x4;
int SWAP_BACK_DISTANCE = 0x8;

class WitnessRandomizer {
public:

	WitnessRandomizer();

	void Randomize(std::vector<int> panels, int flags);
	void SwapPanels(int panel1, int panel2, int flags);

private:
	void SwapPanelData(int panel1, int panel2, int finalOffset, int dataSize);

	Memory _memory;
};