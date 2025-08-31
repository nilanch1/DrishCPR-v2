#include "CPRMetricsCalculator.h"
#include <algorithm>
#include <cmath>

CPRMetricsCalculator::CPRMetricsCalculator() {
    reset();
}

// MODIFIED: Enhanced reset function to ensure complete metric reset
void CPRMetricsCalculator::reset() {
    state = "quietude";
    lastStateChange = millis();
    alertMessage.clear();
    currentRate = 0;
    displayedRate = 0;
    lastValidRateTime = 0;
    
    // Clear all data containers
    compressionPeaks.clear();
    compressionIntervals.clear();
    depthPeaks.clear();
    recoilMins.clear();
    
    // Reset all counters
    goodCompressions = 0;
    totalCompressions = 0;
    goodRecoils = 0;
    incompleteRecoils = 0;
    totalRecoils = 0;
    
    // Clear all history buffers
    valueHistory.clear();
    peakHistory.clear();
    trendBuffer.clear();
    peakTrendBuffer.clear();
    
    // Reset all processing variables
    previousSmoothValue = 0;
    previousPeakValue = 0;
    lastPeakValue = 0;
    smoothedRate = 0;
    currentCompressionPeak = 0;
    currentRecoilMin = 1023; // Max value for minimum tracking
    
    running = true;
    lastCompressionPeak = 0;
    lastCompressionWasOk = false;
    
    // CCF tracking reset - ENHANCED
    cycleStartTime = 0;
    lastActiveTime = 0;
    activeTime = 0;
    totalCycleTime = 0;
    ccf = 0;
    cprCycles = 0;
    lastQuietudeEnterTime = 0;
    validCycleStarted = false;
    seenCompression = false;
    seenRecoil = false;
    
    lastRateUpdateTime = 0;
    
    // Log reset for debugging
    Serial.println("CPR Metrics Calculator: All metrics reset to initial state");
}

void CPRMetricsCalculator::updateParams(const CPRThresholds& newParams) {
    params = newParams;
    reset(); // Reset to apply new parameters
}

CPRStatus CPRMetricsCalculator::detectTrend(float rawValue) {
    if (!running) {
        CPRStatus status;
        status.state = state;
        status.timestamp = millis();
        return status;
    }
    
    unsigned long now = millis();
    lastPeakValue = max(lastPeakValue, rawValue);
    
    // State detection using configured smoothing
    valueHistory.push_back(rawValue);
    if (valueHistory.size() > params.smoothingWindow) {
        valueHistory.pop_front();
    }
    
    float smoothedValue = rawValue;
    if (params.smoothingWindow > 1 && !valueHistory.empty()) {
        float sum = 0;
        for (float val : valueHistory) {
            sum += val;
        }
        smoothedValue = sum / valueHistory.size();
    }
    
    // Peak detection using light smoothing (max of 3 samples)
    peakHistory.push_back(rawValue);
    if (peakHistory.size() > 3) {
        peakHistory.pop_front();
    }
    
    float peakSmoothedValue = rawValue;
    if (!peakHistory.empty()) {
        float sum = 0;
        for (float val : peakHistory) {
            sum += val;
        }
        peakSmoothedValue = sum / peakHistory.size();
    }
    
    // State detection slope calculation
    float slope = 0;
    if (previousSmoothValue != 0) {
        slope = smoothedValue - previousSmoothValue;
    }
    previousSmoothValue = smoothedValue;
    
    // Peak detection slope calculation
    float peakSlope = 0;
    if (previousPeakValue != 0) {
        peakSlope = peakSmoothedValue - previousPeakValue;
    }
    previousPeakValue = peakSmoothedValue;
    
    // Update trend buffers
    trendBuffer.push_back(slope);
    if (trendBuffer.size() > params.trendBufferSize) {
        trendBuffer.pop_front();
    }
    
    peakTrendBuffer.push_back(peakSlope);
    if (peakTrendBuffer.size() > 3) {
        peakTrendBuffer.pop_front();
    }
    
    float avgSlope = 0;
    if (!trendBuffer.empty()) {
        for (float s : trendBuffer) {
            avgSlope += s;
        }
        avgSlope /= trendBuffer.size();
    }
    
    float avgPeakSlope = 0;
    if (!peakTrendBuffer.empty()) {
        for (float s : peakTrendBuffer) {
            avgPeakSlope += s;
        }
        avgPeakSlope /= peakTrendBuffer.size();
    }
    
    float operatingRange = params.c2 - params.r1;
    float quietudeThreshold = params.r1 + (params.quietudePercent * operatingRange);
    float minCompressionAmplitude = params.c1 * 0.5;
    float margin = params.hysteresisMargin * 1000; // Scale for ADC values
    
    String newState = state;
    
    // State transitions based on smoothed values
    if (avgSlope > margin && smoothedValue > minCompressionAmplitude) {
        newState = "compression";
        if (state != "compression") {
            compressionPeaks.push_back(now);
        }
    } else if (avgSlope < -margin * 1.5) {
        newState = "recoil";
        if (state != "recoil") {
            totalRecoils++;
        }
    } else if (smoothedValue <= quietudeThreshold) {
        newState = "quietude";
    }
    
    // Track active time continuously during compression/recoil
    //if (state == "compression" || state == "recoil") {
    //    if (lastActiveTime != 0) {
    //        activeTime += now - lastActiveTime;
    //    }
    //    lastActiveTime = now;
    //} else {
    //    lastActiveTime = 0;
    //}
    
    // Handle state transitions and cycle logic
    if (newState != state) {
        // Track time for CCF calculation
        if (state == "compression" || state == "recoil") {
            activeTime += now - lastStateChange;
        } else if (state == "quietude" && newState != "quietude") {
            // Starting active period
            if (cycleStartTime == 0) {
                cycleStartTime = now;
            }
        }
        
        // End previous state and handle compression quality
        endState();
        
        // Update state
        state = newState;
        lastStateChange = now;
        
        // Handle entering new states for cycle tracking
        if (newState == "compression") {
            seenCompression = true;
            if (!validCycleStarted) {
                validCycleStarted = true;
                cycleStartTime = now;
            }
            currentCompressionPeak = peakSmoothedValue;
            totalCompressions++;
        } else if (newState == "recoil") {
            seenRecoil = true;
            currentRecoilMin = peakSmoothedValue;
        } else if (newState == "quietude") {
            lastQuietudeEnterTime = now;
        }
    }
    
    // Check for cycle completion during quietude
    if (state == "quietude" && lastQuietudeEnterTime != 0 && validCycleStarted) {
        unsigned long quietudeDuration = now - lastQuietudeEnterTime;
        
        if (quietudeDuration >= 2000) { // 2 seconds
            if (seenCompression && seenRecoil) {
                // Complete the cycle
                cprCycles++;
                totalCycleTime = now - cycleStartTime;
                if (totalCycleTime > 0) {
                    ccf = ((float)activeTime / totalCycleTime) * 100.0;
                }
                
                // Reset for next cycle
                cycleStartTime = 0;
                activeTime = 0;
                totalCycleTime = 0;
                validCycleStarted = false;
            }
            
            // Always reset flags after quietude
            seenCompression = false;
            seenRecoil = false;
            lastQuietudeEnterTime = 0;
        }
    }
    
    // Peak/min tracking using peak detection values
    if (state == "compression") {
        currentCompressionPeak = max(currentCompressionPeak, peakSmoothedValue);
    } else if (state == "recoil") {
        currentRecoilMin = min(currentRecoilMin, peakSmoothedValue);
    }
    
    // Update rate and depth every second
    if (now - lastRateUpdateTime >= 1000) {
        updateRateAndDepth(now);
        lastRateUpdateTime = now;
    }
    
    // Prepare and return status
    CPRStatus status;
    status.state = (state == "quietude") ? "pause" : state;
    status.currentRate = displayedRate;
    status.alerts = alertMessage;
    status.rawValue = smoothedValue;
    status.peakValue = lastPeakValue;
    status.thresholds = params;
    status.timestamp = now;
    
    // Metrics
    status.peaks.good = goodCompressions;
    status.peaks.total = totalCompressions;
    status.peaks.ratio = (totalCompressions > 0) ? (float)goodCompressions / totalCompressions : 0;
    status.peaks.isGood = (state == "compression") ? 
        (params.c1 <= currentCompressionPeak && currentCompressionPeak <= params.c2) : false;
    
    if (!depthPeaks.empty()) {
        float sum = 0;
        for (float peak : depthPeaks) {
            sum += peak;
        }
        status.peaks.average = sum / depthPeaks.size();
    }
    
    status.troughs.goodRecoil = goodRecoils;
    status.troughs.incompleteRecoil = incompleteRecoils;
    status.troughs.total = totalRecoils;
    status.troughs.ratio = (totalRecoils > 0) ? (float)goodRecoils / totalRecoils : 0;
    
    status.ccf = ccf;
    status.cycles = cprCycles;
    
    status.currentCompression.peakValue = currentCompressionPeak;
    status.currentCompression.isGood = (state == "compression") ? 
        (params.c1 <= currentCompressionPeak && currentCompressionPeak <= params.c2) : false;
    
    status.currentRecoil.minValue = (currentRecoilMin != 1023) ? currentRecoilMin : 0;
    status.currentRecoil.isGood = (state == "recoil" && currentRecoilMin != 1023) ? 
        (currentRecoilMin <= params.r2) : false;
    
    return status;
}

void CPRMetricsCalculator::endState() {
    if (state == "compression") {
        bool peakOk = (params.c1 <= currentCompressionPeak && currentCompressionPeak <= params.c2);
        depthPeaks.push_back(currentCompressionPeak);
        lastCompressionPeak = currentCompressionPeak;
        lastCompressionWasOk = peakOk;
        
        // Keep only recent peaks (last 100)
        if (depthPeaks.size() > 100) {
            depthPeaks.erase(depthPeaks.begin());
        }
    } else if (state == "recoil") {
        if (currentRecoilMin != 1023) {
            bool recoilOk = (currentRecoilMin <= params.r2);
            
            if (recoilOk) {
                goodRecoils++;
                if (lastCompressionWasOk) {
                    goodCompressions++;
                }
            } else {
                incompleteRecoils++;
            }
            
            recoilMins.push_back(currentRecoilMin);
            
            // Keep only recent recoils (last 100)
            if (recoilMins.size() > 100) {
                recoilMins.erase(recoilMins.begin());
            }
        }
    }
    
    // Reset current peak/min values
    currentCompressionPeak = 0;
    currentRecoilMin = 1023;
}

void CPRMetricsCalculator::updateRateAndDepth(unsigned long now) {
    try {
        // Keep only recent compression peaks (last 10)
        if (compressionPeaks.size() > 10) {
            compressionPeaks.erase(compressionPeaks.begin(), compressionPeaks.end() - 10);
        }
        
        // Calculate rate
        if (compressionPeaks.size() >= 2) {
            std::vector<float> intervals;
            for (size_t i = 1; i < compressionPeaks.size(); i++) {
                float interval = (compressionPeaks[i] - compressionPeaks[i-1]) / 1000.0; // Convert to seconds
                intervals.push_back(interval);
            }
            
            // Get median interval
            std::sort(intervals.begin(), intervals.end());
            float medianInterval = intervals[intervals.size() / 2];
            float clampedInterval = max(0.25f, min(medianInterval, 1.5f));
            float rawRate = 60.0 / clampedInterval;
            
            // Apply smoothing
            float alpha = params.rateSmoothingFactor;
            smoothedRate = (smoothedRate == 0) ? rawRate : alpha * rawRate + (1 - alpha) * smoothedRate;
            
            currentRate = smoothedRate;
            displayedRate = round(smoothedRate);
            lastValidRateTime = now;
        } else {
            displayedRate = 0;
        }
        
        generateAlerts();
        
    } catch (...) {
        alertMessage.clear();
        alertMessage.push_back("⚠️ Rate calculation error");
        reset();
    }
}

void CPRMetricsCalculator::generateAlerts() {
    alertMessage.clear();
    
    int f1 = params.f1;
    int f2 = params.f2;
    int c1 = params.c1;
    int c2 = params.c2;
    int r2 = params.r2;
    
    if (compressionPeaks.empty()) {
        alertMessage.push_back("● No compressions detected");
    } else if (compressionPeaks.size() < 2) {
        alertMessage.push_back("ℹ️ Need more compressions for rate");
    } else if (currentRate < f1) {
        alertMessage.push_back("⚠️ CPR rate too low (" + String(displayedRate) + " < " + String(f1) + ")");
    } else if (currentRate > f2) {
        alertMessage.push_back("⚠️ CPR rate too high (" + String(displayedRate) + " > " + String(f2) + ")");
    }
    
    if (!depthPeaks.empty()) {
        float sum = 0;
        for (float peak : depthPeaks) {
            sum += peak;
        }
        float avgPeak = sum / depthPeaks.size();
        
        if (avgPeak > c2) {
            alertMessage.push_back("⬆️ Be gentle");
        } else if (avgPeak < c1) {
            alertMessage.push_back("⬇️ Press harder");
        }
    }
    
    if (!recoilMins.empty()) {
        float sum = 0;
        for (float recoil : recoilMins) {
            sum += recoil;
        }
        float avgRecoil = sum / recoilMins.size();
        
        if (avgRecoil > r2) {
            alertMessage.push_back("🔼 Release more");
        }
    }
}