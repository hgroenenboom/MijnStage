#include "overlapFFT.h"

overlapFFT::overlapFFT() {
}

overlapFFT::overlapFFT(dsp::FFT *fftFunctionP, int numOverlaps, int fftSize)
	: fft(fftSize)
{
	this->fftFunctionP = fftFunctionP;
	this->numOverlaps = numOverlaps;
	this->fftSize = fftSize;

	//replace all 512 values with bufferSize
	if (fftSize > 512) { //A
		numFFTs = 1;
	}
	else if (512 >= fftSize) {			//B
		numFFTs = 512 / fftSize;
	}	

	inputMemory = forwardBlockDelay(512, 10, 1);
	outputMemory = forwardBlockDelay(512, 10, 1);

	hanningWindow.resize(fftSize);
	createHanningWindow();
}

// [1]
void overlapFFT::pushDataIntoMemoryAndPerformFFTs(AudioSampleBuffer& buffer, int numSamples, int channel) {
	const float* channelData = buffer.getReadPointer(channel, 0);
	float* outBuffer = buffer.getWritePointer(channel, 0);
	this->channel = channel;

	for (int i = 0; i < numSamples; i++) {
		// [1]
		inputMemory.pushNewInput(channelData[i]); //push new data into the inputMemory. The memory will shift it's own pointer.
		
		inputCounter++; //count if enough samples are collected for the FFT.
		inputCounter %= fftSize;

		// [23467]
		if (inputCounter == 0) {
			runThroughFFTs();
			adjustMemoryPointersAndClearOutputMemory();
		}

		outBuffer[i] = getOutputData() * (1.0f / numOverlaps);	
	}
}

// [2,3,4]
void overlapFFT::runThroughFFTs() { // 234
	for (int ovLap = 0; ovLap < numOverlaps; ovLap++) {


		startIndex = ovLap * (fftSize / numOverlaps);
		endIndex = startIndex + fftSize;

		fillFFTBuffer(startIndex, endIndex); //[2]

		applyFFT(); //[3]

		pushFFTDataIntoOutputDelayBuffer(startIndex, endIndex); //[4]
	}
}

// [2]
void overlapFFT::fillFFTBuffer(int startIndex, int endIndex)
{
	zeromem(fft.fftData, sizeof(fft.fftData));

	for (int i = 0; i < fftSize; i++) {
		fft.fftData[i] = inputMemory.readOutputValue(i + startIndex - fftSize * 2);
	}
}

// [3]
void overlapFFT::applyFFT() {
	applyHannningWindowToFftBuffer();

	float lowPassScale = 0.5;

	for (int i = 0; i < fftSize; i++) {
		timeData[i]._Val[0] = fft.fftData[i];
		timeData[i]._Val[1] = 0.0f;
	}

	fftFunctionP->perform(timeData, spectralData, false);

	switch (channel) {
		case 0:
			for (int i = 0; i < fftSize; i++) {
				carToPol(&spectralData[i]._Val[0], &spectralData[i]._Val[1]);
				if (i > 20) spectralData[i]._Val[0] = 0.0f;
				polToCar(&spectralData[i]._Val[0], &spectralData[i]._Val[1]);
			}
			break;
		case 1:
			for (int i = 0; i < fftSize; i++) {
				carToPol(&spectralData[i]._Val[0], &spectralData[i]._Val[1]);
				if (i > 20) spectralData[i]._Val[0] = 0.0f;
				polToCar(&spectralData[i]._Val[0], &spectralData[i]._Val[1]);
			}
			break;
	}

	fftFunctionP->perform(spectralData, timeData, true);

	for (int i = 0; i < fftSize; i++) {
		fft.fftData[i] = timeData[i].real();
	}

	//Apply hanningWindow
	applyHannningWindowToFftBuffer();
}

// [3.1]
void overlapFFT::applyHannningWindowToFftBuffer() {
	for (int i = 0; i < fftSize; i++) { //run through each input amplitude value.
		fft.fftData[i] *= hanningWindow[i];
	}
}

// [3.2]
void overlapFFT::carToPol(float* inReOutM, float* inImOutPhi) {
	float re = *inReOutM;
	float im = *inImOutPhi;

	// phytagoras calculation
	*inReOutM = pow(pow(re, 2.0f) + pow(im, 2.0f), 0.5f);
	*inImOutPhi = atan2(im, re);
}

// [3.3]
void overlapFFT::polToCar(float* inMOutRe, float* inPhiOutIm) {
	float mag = *inMOutRe;
	float phi = *inPhiOutIm;

	*inMOutRe = mag * cos(phi);
	*inPhiOutIm = mag * sin(phi);
}

// [3.4]
void overlapFFT::applyHannningWindowToFftAudio() {
	for (int i = 0; i < fftSize; i++) { //run through each magnitude value.
		fft.fftData[i * 2] *= hanningWindow[i];
	}
}

// [4]
void overlapFFT::pushFFTDataIntoOutputDelayBuffer(int startIndex, int endIndex) {
	for (int i = 0; i < fftSize; i++) {
		outputMemory.addInsertInput(i + startIndex, fft.fftData[i]);
	}
}

// [5]
float overlapFFT::getOutputData() {
	return outputMemory.readOutputValue(inputCounter);
}

// [6, 7]
void overlapFFT::adjustMemoryPointersAndClearOutputMemory() {
	outputMemory.clearBufferData(2 * fftSize, 3 * fftSize);

	inputMemory.adjustPointers(512);
	outputMemory.adjustPointers(512);
}

// Initialize the hanning window
void overlapFFT::createHanningWindow() {
	for (int i = 0; i < fftSize; i++) {
		hanningWindow[i] = sin((float(i) / fftSize) * float_Pi);
	}
}