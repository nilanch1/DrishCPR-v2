#ifndef CPR_METRICS_CALCULATOR_H
#define CPR_METRICS_CALCULATOR_H

#include <Arduino.h>
#include <vector>
#include <deque>

struct CPRThresholds {
    int r1 = 200;  // Recoil low value
    int r2 = 300;  // Recoil high value
    int c1 = 700;  // Compression low value
    int c2 = 900;  // Compression high value
    int f1 = 100;  // Minimum CPR rate
    int f2 = 120;  // Maximum CPR rate
    float quietThreshold = 2.0;
    float quietudePercent = 0.2;
    int smoothingWindow = 3;
    float rateSmoothingFactor = 0.3;
    float compressionGracePeriod = 0.1;
    float hysteresisMargin = 0.01;
    int trendBufferSize = 3;
};

struct CompressionMetrics {
    float average = 0;
    int good = 0;
    int total = 0;
    float ratio = 0;
    bool isGood = false;
};

struct RecoilMetrics {
    int goodRecoil = 0;
    int incompleteRecoil = 0;
    int total = 0;
    float ratio = 0;
};

struct CurrentCompression {
    float peakValue = 0;
    bool isGood = false;
};

struct CurrentRecoil {
    float minValue = 0;
    bool isGood = false;
};

struct CPRStatus {
    String state;
    int currentRate;
    std::vector<String> alerts;
    float rawValue;
    float peakValue;
    CPRThresholds thresholds;
    unsigned long timestamp;
    CompressionMetrics peaks;
    RecoilMetrics troughs;
    float ccf;
    int cycles;
    CurrentCompression currentCompression;
    CurrentRecoil currentRecoil;
};

class CPRMetricsCalculator {
private:
    CPRThresholds params;
    String state;
    unsigned long lastStateChange;
    std::vector<String> alertMessage;
    int currentRate;
    int displayedRate;
    unsigned long lastValidRateTime;
    
    std::vector<unsigned long> compressionPeaks;
    std::vector<unsigned long> compressionIntervals;
    std::vector<float> depthPeaks;
    std::vector<float> recoilMins;
    
    int goodCompressions;
    int totalCompressions;
    int goodRecoils;
    int incompleteRecoils;
    int totalRecoils;
    
    std::deque<float> valueHistory;
    std::deque<float> peakHistory;
    std::deque<float> trendBuffer;
    std::deque<float> peakTrendBuffer;
    
    float previousSmoothValue;
    float previousPeakValue;
    float lastPeakValue;
    float smoothedRate;
    float currentCompressionPeak;
    float currentRecoilMin;
    
    bool running;
    unsigned long lastCompressionPeak;
    bool lastCompressionWasOk;
    
    // CCF tracking variables
    unsigned long cycleStartTime;
    unsigned long lastActiveTime;
    unsigned long activeTime;
    unsigned long totalCycleTime;
    float ccf;
    int cprCycles;
    unsigned long lastQuietudeEnterTime;
    bool validCycleStarted;
    bool seenCompression;
    bool seenRecoil;
    
    unsigned long lastRateUpdateTime;
    
    void endState();
    void updateRateAndDepth(unsigned long now);
    void generateAlerts();

public:
    CPRMetricsCalculator();
    void reset();
    void updateParams(const CPRThresholds& newParams);
    CPRStatus detectTrend(float rawValue);
    CPRThresholds getParams() const { return params; }
    void setRunning(bool run) { running = run; }
    bool isRunning() const { return running; }
};

#endif