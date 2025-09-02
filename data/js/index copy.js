// =============================================
    // GLOBAL STATE AND CONFIGURATION
    // =============================================
    let isRecording = false;
    let currentSessionId = 0;
    let lastKnownState = '';
    let messageQueue = [];
    const MAX_QUEUE_SIZE = 10; // Limit queue size to prevent overflow
    
    // Image loading state
    let imagesLoaded = {
      recoil: false,
      compression: false
    };
    let allImagesReady = false;

    // Audio management
    let audioContext = null;
    let audioEnabled = false;
    let audioFilesLoaded = false;
    let lastAlertTime = 0;
    let isPlayingAudio = false;
    const AUDIO_GAP = 2000; // 2 seconds gap between audio alerts - INCREASED for better gap
    let audioQueue = [];
    let processedAlerts = new Set();

    // Audio file mappings - RESTORED
    const audioMapping = {
      'too low': 'audio-rate-low',
      'Press harder': 'audio-depth-low',
      'too high': 'audio-rate-high',
      'Be gentle': 'audio-depth-high',
      'Release more': 'audio-incomplete-recoil'
    };

    // =============================================
    // AUDIO MANAGEMENT - RESTORED TO ORIGINAL WORKING VERSION
    // =============================================
    function initializeAudio() {
      const audioStatus = document.getElementById('audio-status');
      audioStatus.classList.add('show');
      audioStatus.textContent = 'Checking audio files...';
      
      // Create AudioContext after user interaction
      if (!audioContext) {
        try {
          audioContext = new (window.AudioContext || window.webkitAudioContext)();
        } catch (e) {
          console.warn('Web Audio API not supported:', e);
        }
      }

      // Check if audio files exist before trying to load them
      checkAudioFilesExist().then(availableFiles => {
        if (availableFiles.length === 0) {
          audioStatus.textContent = 'No audio files found - using fallback alerts';
          audioFilesLoaded = false; // Use fallback
          setTimeout(() => {
            audioStatus.classList.remove('show');
          }, 3000);
          return;
        }

        // Load only available audio files
        loadAvailableAudioFiles(availableFiles, audioStatus);
      }).catch(error => {
        console.error('Error checking audio files:', error);
        audioStatus.textContent = 'Audio check failed - using fallback alerts';
        audioFilesLoaded = false;
        setTimeout(() => {
          audioStatus.classList.remove('show');
        }, 3000);
      });

      audioEnabled = true;
    }

    async function checkAudioFilesExist() {
      const audioFiles = [
        { id: 'audio-rate-low', path: '/rateTooLow.mp3' },
        { id: 'audio-rate-high', path: '/rateTooHigh.mp3' },
        { id: 'audio-depth-low', path: '/depthTooLow.mp3' },
        { id: 'audio-depth-high', path: '/depthTooHigh.mp3' },
        { id: 'audio-incomplete-recoil', path: '/incompleteRecoil.mp3' }
      ];

      const availableFiles = [];

      for (const audioFile of audioFiles) {
        try {
          console.log(`🔍 Checking audio file: ${audioFile.path}`);
          
          // Try HEAD request first
          const headResponse = await fetch(audioFile.path, { 
            method: 'HEAD',
            cache: 'no-cache' // Prevent caching issues
          });
          
          console.log(`📊 HEAD response for ${audioFile.path}:`, {
            status: headResponse.status,
            statusText: headResponse.statusText,
            ok: headResponse.ok,
            headers: Object.fromEntries(headResponse.headers.entries())
          });
          
          if (headResponse.ok) {
            availableFiles.push(audioFile);
            console.log(`✅ Audio file confirmed via HEAD: ${audioFile.path}`);
          } else {
            console.warn(`❌ HEAD failed for ${audioFile.path}: ${headResponse.status} ${headResponse.statusText}`);
            
            // Fallback: Try GET request to confirm file exists
            console.log(`🔄 Trying GET request for: ${audioFile.path}`);
            const getResponse = await fetch(audioFile.path, { 
              method: 'GET',
              cache: 'no-cache'
            });
            
            console.log(`📊 GET response for ${audioFile.path}:`, {
              status: getResponse.status,
              statusText: getResponse.statusText,
              ok: getResponse.ok,
              contentType: getResponse.headers.get('content-type'),
              contentLength: getResponse.headers.get('content-length')
            });
            
            if (getResponse.ok) {
              availableFiles.push(audioFile);
              console.log(`✅ Audio file confirmed via GET: ${audioFile.path}`);
            } else {
              console.error(`❌ Both HEAD and GET failed for ${audioFile.path}`);
            }
          }
        } catch (error) {
          console.error(`❌ Network error checking ${audioFile.path}:`, error);
          
          // Even if check fails, try to add it anyway (let audio loading handle the error)
          console.log(`🤞 Adding ${audioFile.path} anyway - will test during loading`);
          availableFiles.push(audioFile);
        }
      }

      console.log(`🎵 Audio file check complete: ${availableFiles.length}/${audioFiles.length} files found`);
      return availableFiles;
    }

    function loadAvailableAudioFiles(availableFiles, audioStatus) {
      if (availableFiles.length === 0) {
        audioStatus.textContent = 'No audio files available';
        audioFilesLoaded = false;
        setTimeout(() => {
          audioStatus.classList.remove('show');
        }, 3000);
        return;
      }

      let loadedCount = 0;
      let failedCount = 0;

      audioStatus.textContent = `Loading ${availableFiles.length} audio files...`;
      console.log(`🎵 Starting to load ${availableFiles.length} audio files from SPIFFS`);

      availableFiles.forEach(audioFile => {
        const audio = document.getElementById(audioFile.id);
        if (audio) {
          console.log(`🔄 Loading audio: ${audioFile.path} into element: ${audioFile.id}`);
          
          // Set the source
          audio.src = audioFile.path;

          audio.addEventListener('canplaythrough', () => {
            loadedCount++;
            console.log(`✅ Audio loaded successfully: ${audioFile.path} (readyState: ${audio.readyState})`);
            updateAudioLoadingStatus(loadedCount, failedCount, availableFiles.length, audioStatus);
          }, { once: true });

          audio.addEventListener('loadeddata', () => {
            console.log(`📊 Audio data loaded: ${audioFile.path} (duration: ${audio.duration}s, readyState: ${audio.readyState})`);
          }, { once: true });

          audio.addEventListener('error', (e) => {
            failedCount++;
            console.error(`❌ Failed to load audio: ${audioFile.path}`, {
              error: e.target.error,
              networkState: e.target.networkState,
              readyState: e.target.readyState,
              src: e.target.src,
              errorCode: e.target.error ? e.target.error.code : 'unknown',
              errorMessage: e.target.error ? e.target.error.message : 'unknown'
            });
            updateAudioLoadingStatus(loadedCount, failedCount, availableFiles.length, audioStatus);
          }, { once: true });

          // Start loading
          console.log(`🚀 Initiating load for: ${audioFile.path}`);
          audio.load();
        } else {
          failedCount++;
          console.error(`❌ Audio element not found: ${audioFile.id}`);
          updateAudioLoadingStatus(loadedCount, failedCount, availableFiles.length, audioStatus);
        }
      });

      // Timeout after 15 seconds (increased from 10)
      setTimeout(() => {
        if (loadedCount + failedCount < availableFiles.length) {
          console.warn(`⏰ Audio loading timeout - ${loadedCount} loaded, ${failedCount} failed, ${availableFiles.length - loadedCount - failedCount} still loading`);
          audioFilesLoaded = loadedCount > 0;
          audioStatus.textContent = `Audio loading timeout - ${loadedCount}/${availableFiles.length} loaded`;
          setTimeout(() => {
            audioStatus.classList.remove('show');
          }, 3000);
        }
      }, 15000);
    }

    function updateAudioLoadingStatus(loadedCount, failedCount, totalCount, audioStatus) {
      if (loadedCount + failedCount >= totalCount) {
        if (loadedCount > 0) {
          audioFilesLoaded = true;
          audioStatus.textContent = `Audio ready ✓ (${loadedCount}/${totalCount} files)`;
          console.log(`Audio loading complete: ${loadedCount}/${totalCount} files loaded`);
        } else {
          audioFilesLoaded = false;
          audioStatus.textContent = 'No audio files loaded - using fallback';
        }
        
        setTimeout(() => {
          audioStatus.classList.remove('show');
        }, 2000);
      } else {
        audioStatus.textContent = `Loading audio... ${loadedCount + failedCount}/${totalCount}`;
      }
    }

    // ENHANCED: Audio playback with improved gap control
    function playAudioAlert(alertText) {
      if (!audioEnabled || !isRecording) {
        return;
      }

      // If no audio files loaded, use fallback audio alerts
      if (!audioFilesLoaded) {
        playFallbackAudio(alertText);
        return;
      }

      const currentTime = Date.now();
      
      // IMPROVED: Check if enough time has passed since last audio AND not currently playing
      if (currentTime - lastAlertTime < AUDIO_GAP || isPlayingAudio) {
        console.log(`Audio blocked: Gap not met (${currentTime - lastAlertTime}ms < ${AUDIO_GAP}ms) or currently playing: ${isPlayingAudio}`);
        return;
      }

      // Find matching audio file
      let audioId = null;
      for (const [key, value] of Object.entries(audioMapping)) {
        if (alertText.includes(key)) {
          audioId = value;
          break;
        }
      }

      if (!audioId) {
        console.log('No audio mapping found for alert:', alertText);
        playFallbackAudio(alertText);
        return;
      }

      const audio = document.getElementById(audioId);
      if (audio && audio.src) {
        try {
          // Reset audio to beginning
          audio.currentTime = 0;
          
          // Set flags BEFORE playing
          isPlayingAudio = true;
          lastAlertTime = currentTime;
          
          // Play audio
          const playPromise = audio.play();
          
          if (playPromise !== undefined) {
            playPromise.then(() => {
              console.log(`🎵 Playing audio for: ${alertText} (file: ${audio.src})`);
              
              // Enhanced end handlers
              const endHandler = () => {
                isPlayingAudio = false;
                console.log(`🎵 Audio ended for: ${alertText}`);
                // Remove all listeners to prevent memory leaks
                audio.removeEventListener('ended', endHandler);
                audio.removeEventListener('pause', endHandler);
                audio.removeEventListener('error', endHandler);
              };
              
              audio.addEventListener('ended', endHandler, { once: true });
              audio.addEventListener('pause', endHandler, { once: true });
              audio.addEventListener('error', endHandler, { once: true });
              
            }).catch(error => {
              console.warn('Audio play failed, using fallback:', error);
              playFallbackAudio(alertText);
              isPlayingAudio = false;
            });
          }
        } catch (error) {
          console.warn('Error playing audio, using fallback:', error);
          playFallbackAudio(alertText);
          isPlayingAudio = false;
        }
      } else {
        console.log('Audio element not available, using fallback');
        playFallbackAudio(alertText);
      }
    }

    function playFallbackAudio(alertText) {
      // Use browser's built-in audio or visual feedback as fallback
      const currentTime = Date.now();
      
      if (currentTime - lastAlertTime < AUDIO_GAP || isPlayingAudio) {
        return;
      }

      // Create a simple beep using Web Audio API
      if (audioContext) {
        try {
          const oscillator = audioContext.createOscillator();
          const gainNode = audioContext.createGain();
          
          oscillator.connect(gainNode);
          gainNode.connect(audioContext.destination);
          
          // Set frequency based on alert type
          let frequency = 1000; // Default
          if (alertText.includes('too low') || alertText.includes('Press harder')) {
            frequency = 800;  // Lower for low rate/depth
          } else if (alertText.includes('too high') || alertText.includes('Be gentle')) {
            frequency = 1200; // Higher for high rate/depth
          } else if (alertText.includes('Release more')) {
            frequency = 900;  // Medium for recoil
          }
          
          oscillator.frequency.setValueAtTime(frequency, audioContext.currentTime);
          oscillator.type = 'sine';
          
          gainNode.gain.setValueAtTime(0.1, audioContext.currentTime);
          gainNode.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + 0.3);
          
          isPlayingAudio = true;
          oscillator.start(audioContext.currentTime);
          oscillator.stop(audioContext.currentTime + 0.3);
          
          // Reset playing flag after duration
          setTimeout(() => {
            isPlayingAudio = false;
          }, 300);
          
          lastAlertTime = currentTime;
          console.log(`Fallback beep played for: ${alertText}`);
          
        } catch (error) {
          console.warn('Fallback audio also failed:', error);
        }
      } else {
        // Visual feedback only
        console.log(`Audio alert (visual only): ${alertText}`);
        lastAlertTime = currentTime;
      }
    }

    // MODIFIED: Process audio alerts only during recording with improved gap control
    function processAudioAlerts(alerts) {
      if (!alerts || alerts.length === 0 || !isRecording) {
        return; // IMPORTANT: Only process audio during training
      }

      // Create a unique key for current alerts to avoid repetition
      const alertKey = alerts.join('|');
      const currentTime = Date.now();
      
      // Remove old processed alerts (older than 10 seconds)
      const ALERT_MEMORY_TIME = 10000;
      const expiredKeys = Array.from(processedAlerts).filter(key => {
        const [, timestamp] = key.split('::');
        return currentTime - parseInt(timestamp) > ALERT_MEMORY_TIME;
      });
      expiredKeys.forEach(key => processedAlerts.delete(key));
      
      // Check if we've already processed these alerts recently
      const recentKey = `${alertKey}::${currentTime}`;
      const hasRecentSimilar = Array.from(processedAlerts).some(key => {
        const [alerts] = key.split('::');
        return alerts === alertKey;
      });
      
      if (hasRecentSimilar) {
        return;
      }
      
      // Process first alert that has audio mapping
      for (const alert of alerts) {
        let foundMapping = false;
        for (const key of Object.keys(audioMapping)) {
          if (alert.includes(key)) {
            playAudioAlert(alert);
            processedAlerts.add(recentKey);
            foundMapping = true;
            break;
          }
        }
        if (foundMapping) {
          break; // Only play one alert at a time
        }
      }
    }

    // =============================================
    // MESSAGE QUEUE MANAGEMENT
    // =============================================
    function addToQueue(message) {
      messageQueue.push(message);
      
      // Purge old messages if queue is too large (FIFO)
      if (messageQueue.length > MAX_QUEUE_SIZE) {
        const removed = messageQueue.splice(0, messageQueue.length - MAX_QUEUE_SIZE);
        console.log(`Queue overflow: purged ${removed.length} old messages`);
      }
      
      processQueue();
    }

    function processQueue() {
      if (messageQueue.length === 0) return;
      
      const message = messageQueue.shift();
      handleMessage(message);
      
      // Process next message after a short delay to prevent blocking
      if (messageQueue.length > 0) {
        setTimeout(processQueue, 10);
      }
    }

    function handleMessage(data) {
      try {
        if (data.type === 'recording_status') {
          handleRecordingStatus(data);
        } else if (data.type === 'state_update') {
          handleStateUpdate(data);
        }
      } catch (error) {
        console.error('Error handling message:', error);
      }
    }

    // =============================================
    // IMAGE MANAGEMENT
    // =============================================
    function handleImageLoad(imageType) {
      imagesLoaded[imageType] = true;
      checkAllImagesLoaded();
    }

    function handleImageError(imageType, imagePath) {
      console.warn(`Failed to load ${imageType} image: ${imagePath}`);
      imagesLoaded[imageType] = true; // Consider it "loaded" to proceed
      checkAllImagesLoaded();
    }

    function checkAllImagesLoaded() {
      if (imagesLoaded.recoil && imagesLoaded.compression && !allImagesReady) {
        allImagesReady = true;
        console.log('All images loaded - animations ready');
      }
    }

    // =============================================
    // STATE VISUALIZATION
    // =============================================
    function updateStateVisualization(newState) {
      if (!allImagesReady) return;
      
      const normalizedState = (newState || '').toLowerCase().trim();
      if (normalizedState === lastKnownState && normalizedState !== 'compression' && normalizedState !== 'recoil') {
      return; // Skip only for quietude repeats
    }

      try {
        const imageContainer = document.getElementById("image-container");
        const recoilImage = document.getElementById("recoil-image");
        const compressionImage = document.getElementById("compression-image");
        const stateIndicator = document.getElementById("state-indicator");
        
        // Remove all state classes
        imageContainer.classList.remove('compression', 'recoil', 'quietude');
        stateIndicator.classList.remove('compression', 'recoil', 'quietude');
        
        // Hide all images first
        recoilImage.classList.remove('active');
        compressionImage.classList.remove('active');
        
        // Apply new state
        if (normalizedState === "compression" || normalizedState === "compress") {
          compressionImage.classList.add('active');
          imageContainer.classList.add('compression');
          stateIndicator.classList.add('compression');
          stateIndicator.textContent = 'Compressing';
          
        } else if (normalizedState === "recoil" || normalizedState === "release") {
          recoilImage.classList.add('active');
          imageContainer.classList.add('recoil');
          stateIndicator.classList.add('recoil');
          stateIndicator.textContent = 'Recoiling';
          
        } else {
          recoilImage.classList.add('active');
          imageContainer.classList.add('quietude');
          stateIndicator.classList.add('quietude');
          stateIndicator.textContent = 'Quietude';
        }
        
        lastKnownState = normalizedState;
        
      } catch (error) {
        console.error('Error updating state visualization:', error);
      }
    }

    // =============================================
    // METRICS UPDATE
    // =============================================
    function updateMetrics(data) {
      try {
        // Update rate
        const rateElement = document.getElementById("rate-value");
        if (data.currentRate && data.currentRate > 0) {
          rateElement.innerHTML = `${Math.round(data.currentRate)}<span class="metric-unit">/min</span>`;
        } else {
          rateElement.innerHTML = `--<span class="metric-unit">/min</span>`;
        }
        
        // Update CCF
        const ccfElement = document.getElementById("ccf-value");
        if (data.metrics?.ccf) {
          ccfElement.innerHTML = `${Math.round(data.metrics.ccf)}<span class="metric-unit">%</span>`;
        } else {
          ccfElement.innerHTML = `--<span class="metric-unit">%</span>`;
        }
        
        // Update cycles
        document.getElementById("cycle-count").textContent = data.metrics?.cycles ?? '--';
        
        // Update compression quality
        if (data.metrics?.peaks) {
          const good = data.metrics.peaks.good || 0;
          const total = data.metrics.peaks.total || 0;
          document.getElementById("compression-quality").textContent = total > 0 ? `${good}/${total}` : '--';
        }
        
        // Update recoil quality
        if (data.metrics?.troughs) {
          const good = data.metrics.troughs.good_recoil || 0;
          const total = data.metrics.troughs.total || 0;
          document.getElementById("recoil-quality").textContent = total > 0 ? `${good}/${total}` : '--';
        }
        
      } catch (error) {
        console.error('Error updating metrics:', error);
      }
    }

    // =============================================
    // ALERT MANAGEMENT - ONLY DURING TRAINING SESSIONS
    // =============================================
    function updateAlerts(alerts) {
      const alertBox = document.getElementById("alert-box");
      
      try {
        // IMPORTANT: Only show alerts if recording/training is active
        if (isRecording && alerts && alerts.length > 0) {
          const alertText = alerts.join(" | ");
          alertBox.textContent = alertText;
          
          // Determine alert level
          if (alertText.includes("too low") || alertText.includes("Press harder") || alertText.includes("Release more")) {
            alertBox.className = "alert-red";
          } else if (alertText.includes("too high") || alertText.includes("Be gentle")) {
            alertBox.className = "alert-yellow";
          } else if (alertText.includes("No compressions")) {
            alertBox.className = "alert-gray";
          } else {
            alertBox.className = "alert-blue";
          }
          
          // Process audio alerts (only during training)
          processAudioAlerts(alerts);
        } else if (isRecording) {
          // Training active but no alerts
          alertBox.textContent = "CPR monitoring active - performing well";
          alertBox.className = "alert-green";
        } else {
          // Not training - show ready message
          alertBox.textContent = "Ready to start training session";
          alertBox.className = "alert-gray";
        }
      } catch (error) {
        console.error('Error updating alerts:', error);
        alertBox.textContent = isRecording ? "Monitoring..." : "Ready to start training session";
        alertBox.className = "alert-gray";
      }
    }

    // =============================================
    // MESSAGE HANDLERS
    // =============================================
    function handleRecordingStatus(data) {
      isRecording = data.is_recording;
      currentSessionId = data.session_id || 0;
      
      const recordButton = document.getElementById("record-button");
      const recordingStatus = document.getElementById("recording-status");
      const sessionId = document.getElementById("session-id");
      
      recordButton.textContent = isRecording ? "Stop Training" : "Start Training";
      recordButton.className = isRecording ? "stop" : "";
      
      recordingStatus.textContent = isRecording ? 
        `Training session ${currentSessionId} active` : "Ready to start training session";
      
      sessionId.textContent = currentSessionId || '--';
      
      // IMPORTANT: Clear alerts when training stops
      if (!isRecording) {
        const alertBox = document.getElementById("alert-box");
        alertBox.textContent = "Ready to start training session";
        alertBox.className = "alert-gray";
        
        // Clear processed alerts and reset audio flags
        processedAlerts.clear();
        isPlayingAudio = false;
        lastAlertTime = 0;
      }
    }

    function handleStateUpdate(data) {
      updateMetrics(data);
      updateStateVisualization(data.state);
      updateAlerts(data.alerts);
    }

    // =============================================
    // RECORDING CONTROL - ENHANCED: Reset metrics on start
    // =============================================
    function toggleRecording() {
      // Initialize audio when starting training (user interaction required)
      if (!isRecording && !audioEnabled) {
        initializeAudio();
      }
      
      fetch('/start_stop', {
          method: 'POST',
          headers: {
              'Content-Type': 'application/json',
          }
      })
      .then(response => response.json())
      .then(data => {
          handleRecordingStatus(data);
          
          // ENHANCED: Reset metrics display when starting new session
          if (isRecording) {
            // Clear processed alerts
            processedAlerts.clear();
            
            // Reset audio flags
            isPlayingAudio = false;
            lastAlertTime = 0;
            
            // Reset all metric displays to initial state
            document.getElementById("rate-value").innerHTML = `--<span class="metric-unit">/min</span>`;
            document.getElementById("ccf-value").innerHTML = `--<span class="metric-unit">%</span>`;
            document.getElementById("cycle-count").textContent = '--';
            document.getElementById("compression-quality").textContent = '--';
            document.getElementById("recoil-quality").textContent = '--';
            
            console.log('🚀 Training session started - metrics display reset, audio system ready');
          } else {
            console.log('⏹️ Training session stopped');
          }
      })
      .catch(error => {
          console.error('Recording error:', error);
          document.getElementById("recording-status").textContent = "Error controlling recording";
      });
    }

    // =============================================
    // MENU MANAGEMENT
    // =============================================
    function toggleMenu() {
      document.getElementById("dropdown-menu").classList.toggle("show");
    }

    // Close menu when clicking outside
    window.onclick = function(event) {
      if (!event.target.closest('.menu-button') && !event.target.closest('.dropdown-menu')) {
        document.getElementById("dropdown-menu").classList.remove("show");
      }
    }

    // =============================================
    // WEBSOCKET CONNECTION
    // =============================================
    let socket;
    let reconnectAttempts = 0;
    let maxReconnectAttempts = 5;
    let reconnectDelay = 2000;

    function connectWebSocket() {
      try {
        socket = new WebSocket(`ws://${window.location.hostname}/ws`);
        
        socket.onopen = function() {
          document.getElementById("connection-status").textContent = "Connected";
          document.getElementById("connection-status").className = "connection-status connected";
          reconnectAttempts = 0;
          console.log('WebSocket connected');
        };

        socket.onmessage = function(event) {
          try {
            const data = JSON.parse(event.data);
            addToQueue(data); // Use queue system to prevent overflow
          } catch (error) {
            console.error('Error parsing WebSocket message:', error);
          }
        };

        socket.onclose = function(event) {
          document.getElementById("connection-status").textContent = "Disconnected";
          document.getElementById("connection-status").className = "connection-status disconnected";
          document.getElementById("alert-box").className = "alert-gray";
          document.getElementById("alert-box").textContent = "Connection lost - attempting to reconnect...";
          
          // Attempt to reconnect
          if (reconnectAttempts < maxReconnectAttempts) {
            reconnectAttempts++;
            setTimeout(connectWebSocket, reconnectDelay * reconnectAttempts);
          }
        };

        socket.onerror = function(error) {
          console.error('WebSocket error:', error);
          document.getElementById("connection-status").textContent = "Error";
          document.getElementById("connection-status").className = "connection-status disconnected";
        };
        
      } catch (error) {
        console.error('Failed to create WebSocket:', error);
        setTimeout(connectWebSocket, reconnectDelay);
      }
    }

    // =============================================
    // INITIALIZATION
    // =============================================
    window.addEventListener('load', function() {
      console.log('CPR Dashboard loaded');
      
      // Initialize session display
      document.getElementById("session-id").textContent = '--';
      
      // Connect WebSocket
      connectWebSocket();
      
      // Check image loading status periodically
      setTimeout(() => {
        if (!allImagesReady) {
          console.warn('Images taking longer than expected to load');
          // Force reload images
          const recoilImg = document.getElementById('recoil-image');
          const compressionImg = document.getElementById('compression-image');
          recoilImg.src = recoilImg.src + '?t=' + Date.now();
          compressionImg.src = compressionImg.src + '?t=' + Date.now();
        }
      }, 3000);
    });

    // Expose essential functions globally for debugging
    window.cprDashboard = {
      toggleRecording,
      getQueueStatus: () => ({
        queueLength: messageQueue.length,
        maxQueueSize: MAX_QUEUE_SIZE,
        isRecording: isRecording,
        currentSessionId: currentSessionId,
        audioEnabled: audioEnabled,
        audioFilesLoaded: audioFilesLoaded,
        isPlayingAudio: isPlayingAudio,
        lastAlertTime: lastAlertTime
      }),
      testAudio: (alertText) => {
        if (audioEnabled && audioFilesLoaded) {
          playAudioAlert(alertText);
        } else if (audioEnabled) {
          playFallbackAudio(alertText);
        } else {
          console.log('Audio not enabled. Start training first.');
        }
      },
      checkAudioFiles: async () => {
        console.log('🔍 Manual audio file check...');
        const available = await checkAudioFilesExist();
        console.log('Available audio files:', available);
        return available;
      },
      testDirectAudioAccess: async () => {
        // Test direct access to audio files
        const files = ['/rateTooLow.mp3', '/rateTooHigh.mp3', '/depthTooLow.mp3', '/depthTooHigh.mp3', '/incompleteRecoil.mp3'];
        console.log('🧪 Testing direct access to audio files...');
        
        for (const file of files) {
          try {
            const response = await fetch(file);
            console.log(`${file}: ${response.status} ${response.statusText} (${response.headers.get('content-type')}, ${response.headers.get('content-length')} bytes)`);
          } catch (error) {
            console.error(`${file}: ERROR - ${error.message}`);
          }
        }
      },
      forceLoadAudio: () => {
        // Force load audio files regardless of check
        console.log('🔄 Force loading audio files...');
        const audioFiles = [
          { id: 'audio-rate-low', path: '/rateTooLow.mp3' },
          { id: 'audio-rate-high', path: '/rateTooHigh.mp3' },
          { id: 'audio-depth-low', path: '/depthTooLow.mp3' },
          { id: 'audio-depth-high', path: '/depthTooHigh.mp3' },
          { id: 'audio-incomplete-recoil', path: '/incompleteRecoil.mp3' }
        ];
        
        const audioStatus = document.getElementById('audio-status');
        audioStatus.classList.add('show');
        loadAvailableAudioFiles(audioFiles, audioStatus);
      },
      listAllFiles: async () => {
        try {
          const response = await fetch('/files_api');
          const data = await response.json();
          console.log('All files on device:', data.files);
          return data.files;
        } catch (error) {
          console.error('Error listing files:', error);
          return [];
        }
      },
      forceResetMetrics: () => {
        // Manual reset function for debugging
        document.getElementById("rate-value").innerHTML = `--<span class="metric-unit">/min</span>`;
        document.getElementById("ccf-value").innerHTML = `--<span class="metric-unit">%</span>`;
        document.getElementById("cycle-count").textContent = '--';
        document.getElementById("compression-quality").textContent = '--';
        document.getElementById("recoil-quality").textContent = '--';
        console.log('Metrics display manually reset');
      },
      testAudioGap: () => {
        // Test function to verify audio gap working
        console.log('Testing audio gap...');
        playAudioAlert('Press harder');
        setTimeout(() => playAudioAlert('too low'), 1000); // Should be blocked
        setTimeout(() => playAudioAlert('too high'), 3000); // Should play
      }
    };