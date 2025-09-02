// =============================================
    // FIXED GLOBAL STATE WITH PROPER SYNCHRONIZATION
    // =============================================
    let isRecording = false;
    let currentSessionId = 0;
    let lastKnownState = '';
    let messageQueue = [];
    const MAX_QUEUE_SIZE = 10;
    
    // Recording state management - CRITICAL FIX
    let recordingActionInProgress = false;
    let lastRecordingResponse = null;
    
    // WebSocket connection management - FIXED
    let socket = null;
    let animSocket = null;
    let reconnectAttempts = 0;
    let animReconnectAttempts = 0;
    let maxReconnectAttempts = 5;
    let reconnectDelay = 2000;
    let socketConnected = false;
    let animSocketConnected = false;
    
    // Prevent multiple simultaneous connection attempts
    let connectingSocket = false;
    let connectingAnimSocket = false;
    
    // Image loading
    let imagesLoaded = { recoil: false, compression: false };
    let allImagesReady = false;

    // REAL-TIME AUDIO SYSTEM
    let audioEnabled = false;
    let audioInitialized = false;
    let lastAlertTime = 0;
    let isPlayingAudio = false;
    const AUDIO_GAP = 3000; // Fixed 3-second gap between any audio
    let audioContext = null;
    let currentAlerts = []; // Real-time alerts only
    let currentAlertIndex = 0; // For cycling through multiple alerts
    let audioInterval = null; // For 3-second interval timer

    // ENHANCED CONNECTIVITY MONITORING
    let internetConnected = false;
    let wifiConnected = false;
    let cloudEnabled = false;
    let cloudSyncInProgress = false;
    let lastInternetCheck = 0;
    let internetCheckInterval = null;
    const INTERNET_CHECK_FREQUENCY = 15000; // Check every 15 seconds

    // Exact MP3 file mappings based on your SPIFFS files
    const audioFiles = {
      'rateTooLow': '/rateTooLow.mp3',
      'rateTooHigh': '/rateTooHigh.mp3', 
      'depthTooLow': '/depthTooLow.mp3',
      'depthTooHigh': '/depthTooHigh.mp3',
      'incompleteRecoil': '/incompleteRecoil.mp3'
    };

    // Alert text to MP3 file mapping
    const alertToAudioMap = {
      'CPR rate too low': 'rateTooLow',
      'too low': 'rateTooLow',
      'Press harder': 'depthTooLow',
      'CPR rate too high': 'rateTooHigh', 
      'too high': 'rateTooHigh',
      'Be gentle': 'depthTooHigh',
      'Release more': 'incompleteRecoil'
    };

    // =============================================
    // FIXED MESSAGE QUEUE AND PROCESSING FUNCTIONS
    // =============================================
    function addToQueue(data) {
      messageQueue.push(data);
      
      if (messageQueue.length > MAX_QUEUE_SIZE) {
        messageQueue.shift();
      }
      
      processQueuedMessage(data);
    }

    function processQueuedMessage(data) {
      try {
        if (data.type === 'metrics') {
          updateMetricsDisplay(data);
          updateAudioAlerts(data.alerts || []);
        } else if (data.type === 'recording_status') {
          // CRITICAL FIX: Properly update global state
          updateRecordingStatus(data);
        } else if (data.type === 'network_status') {
          handleNetworkStatusUpdate(data);
        }
      } catch (error) {
        console.error('Error processing queued message:', error, data);
      }
    }

    function updateMetricsDisplay(data) {
      try {
        const rateElement = document.getElementById('rate-value');
        if (rateElement) {
          rateElement.innerHTML = (data.rate || 0) + '<span class="metric-unit">/min</span>';
        }
        
        const ccfElement = document.getElementById('ccf-value');
        if (ccfElement) {
          ccfElement.innerHTML = (data.ccf || 0).toFixed(1) + '<span class="metric-unit">%</span>';
        }
        
        const cycleElement = document.getElementById('cycle-count');
        if (cycleElement) {
          cycleElement.textContent = data.cycles || 0;
        }
        
        const compressionElement = document.getElementById('compression-quality');
        if (compressionElement) {
          const ratio = data.good_compressions && data.total_compressions ? 
            (data.good_compressions / data.total_compressions) : 0;
          compressionElement.textContent = (ratio * 100).toFixed(0) + '%';
        }
        
        const recoilElement = document.getElementById('recoil-quality');
        if (recoilElement) {
          const ratio = data.good_recoils && data.total_recoils ? 
            (data.good_recoils / data.total_recoils) : 0;
          recoilElement.textContent = (ratio * 100).toFixed(0) + '%';
        }
        
        updateAlertBox(data.alerts || []);
        
      } catch (error) {
        console.error('Error updating metrics display:', error);
      }
    }

    // CRITICAL FIX: Proper recording status synchronization
    function updateRecordingStatus(data) {
      try {
        console.log('Recording status update:', data);
        
        const statusElement = document.getElementById('recording-status');
        const recordButton = document.getElementById('record-button');
        const sessionElement = document.getElementById('session-id');
        
        if (!statusElement || !recordButton) {
          console.error('Missing UI elements for recording status update');
          return;
        }
        
        // CRITICAL: Update global state from server response
        const wasRecording = isRecording;
        isRecording = Boolean(data.is_recording);
        currentSessionId = data.session_id || 0;
        
        // Clear any pending recording action
        recordingActionInProgress = false;
        lastRecordingResponse = data;
        
        console.log('State transition:', wasRecording, '->', isRecording, 'Session:', currentSessionId);
        
        // Update UI based on ACTUAL server state
        if (isRecording) {
          statusElement.textContent = `Recording Session ${currentSessionId}`;
          statusElement.style.color = '#f44336';
          statusElement.style.background = 'rgba(244, 67, 54, 0.1)';
          statusElement.style.borderColor = 'rgba(244, 67, 54, 0.3)';
          
          recordButton.textContent = 'Stop Training';
          recordButton.className = 'stop';
          recordButton.disabled = false;
          
          // Start audio interval for recording
          if (audioEnabled && !audioInterval) {
            startAudioInterval();
          }
        } else {
          statusElement.textContent = 'Ready to start training session';
          statusElement.style.color = '#4CAF50';
          statusElement.style.background = 'rgba(76, 175, 80, 0.1)';
          statusElement.style.borderColor = 'rgba(76, 175, 80, 0.3)';
          
          recordButton.textContent = 'Start Training';
          recordButton.className = '';
          recordButton.disabled = false;
          
          // Stop audio interval when not recording
          stopAudioInterval();
        }
        
        if (sessionElement) {
          sessionElement.textContent = currentSessionId || '--';
        }
        
        console.log('Recording status UI updated successfully');
        
      } catch (error) {
        console.error('Error updating recording status:', error);
        // Reset button state on error
        const recordButton = document.getElementById('record-button');
        if (recordButton) {
          recordButton.disabled = false;
        }
        recordingActionInProgress = false;
      }
    }

    function updateAlertBox(alerts) {
      try {
        const alertBox = document.getElementById('alert-box');
        if (!alertBox) return;
        
        alertBox.className = '';
        
        if (!alerts || alerts.length === 0) {
          if (isRecording) {
            alertBox.className = 'alert-green';
            alertBox.textContent = 'Good technique - keep going!';
          } else {
            alertBox.className = 'alert-gray';
            alertBox.textContent = 'Ready to start training session';
          }
          return;
        }
        
        const alert = alerts[0];
        
        if (alert.includes('rate too low') || alert.includes('rate too high')) {
          alertBox.className = 'alert-red';
        } else if (alert.includes('Press harder') || alert.includes('Be gentle') || alert.includes('Release more')) {
          alertBox.className = 'alert-yellow';
        } else if (alert.includes('No compressions')) {
          alertBox.className = 'alert-blue';
        } else {
          alertBox.className = 'alert-gray';
        }
        
        alertBox.textContent = alert;
      } catch (error) {
        console.error('Error updating alert box:', error);
      }
    }

    function updateAudioAlerts(alerts) {
      if (!audioEnabled || !alerts || alerts.length === 0) return;
      
      currentAlerts = alerts.filter(alert => 
        alert.includes('rate too low') || 
        alert.includes('rate too high') || 
        alert.includes('Press harder') || 
        alert.includes('Be gentle') || 
        alert.includes('Release more')
      );
    }

    // =============================================
    // AUDIO SYSTEM FUNCTIONS
    // =============================================
    function initAudio() {
      if (audioInitialized) return Promise.resolve();
      
      console.log('Initializing MP3 audio system...');
      
      return new Promise((resolve) => {
        audioEnabled = true;
        audioInitialized = true;
        
        console.log('MP3 audio system ready');
        resolve();
      });
    }

    function playAudio(alertType) {
      if (!audioEnabled || isPlayingAudio) return;
      
      const now = Date.now();
      if (now - lastAlertTime < AUDIO_GAP) return;
      
      const audioFile = audioFiles[alertType];
      if (!audioFile) {
        console.warn('No audio file found for alert type:', alertType);
        return;
      }
      
      console.log('Playing audio:', alertType, audioFile);
      
      const audio = new Audio(audioFile);
      audio.volume = 0.7;
      
      isPlayingAudio = true;
      lastAlertTime = now;
      
      audio.play().then(() => {
        console.log('Audio started successfully:', alertType);
      }).catch(error => {
        console.warn('Audio play failed:', error);
        isPlayingAudio = false;
      });
      
      audio.onended = () => {
        isPlayingAudio = false;
        console.log('Audio ended:', alertType);
      };
      
      audio.onerror = () => {
        isPlayingAudio = false;
        console.warn('Audio error for:', alertType);
      };
    }

    function processCurrentAlerts() {
      if (!audioEnabled || currentAlerts.length === 0 || isPlayingAudio) return;
      
      const now = Date.now();
      if (now - lastAlertTime < AUDIO_GAP) return;
      
      const alert = currentAlerts[currentAlertIndex % currentAlerts.length];
      
      for (const [text, audioType] of Object.entries(alertToAudioMap)) {
        if (alert.includes(text)) {
          playAudio(audioType);
          break;
        }
      }
      
      currentAlertIndex = (currentAlertIndex + 1) % currentAlerts.length;
    }

    function startAudioInterval() {
      if (audioInterval) return;
      
      audioInterval = setInterval(() => {
        if (isRecording && currentAlerts.length > 0) {
          processCurrentAlerts();
        }
      }, AUDIO_GAP);
      
      console.log('Audio interval started (3-second intervals)');
    }

    function stopAudioInterval() {
      if (audioInterval) {
        clearInterval(audioInterval);
        audioInterval = null;
        console.log('Audio interval stopped');
      }
    }

    // =============================================
    // ENHANCED STATUS INDICATOR FUNCTIONS
    // =============================================
    function updateInternetStatus(connected, checking = false) {
      const internetStatusEl = document.getElementById('internet-status');
      const ledEl = internetStatusEl.querySelector('.led-indicator');
      const textEl = internetStatusEl.querySelector('.status-text');
      
      internetConnected = connected;
      
      internetStatusEl.classList.remove('connected', 'disconnected', 'checking');
      ledEl.classList.remove('green', 'red', 'yellow');
      
      if (checking) {
        internetStatusEl.classList.add('checking');
        ledEl.classList.add('yellow');
        textEl.innerHTML = 'Internet<br>Checking...';
      } else if (connected) {
        internetStatusEl.classList.add('connected');
        ledEl.classList.add('green');
        textEl.innerHTML = 'Internet<br>Connected';
      } else {
        internetStatusEl.classList.add('disconnected');
        ledEl.classList.add('red');
        textEl.innerHTML = 'Internet<br>Offline';
      }
      
      console.log('Internet status:', checking ? 'checking' : (connected ? 'connected' : 'disconnected'));
    }

    function updateCloudStatus(enabled, syncing = false, provider = '') {
      const cloudStatusEl = document.getElementById('cloud-status');
      const ledEl = cloudStatusEl.querySelector('.led-indicator');
      const textEl = cloudStatusEl.querySelector('.status-text');
      
      cloudEnabled = enabled;
      cloudSyncInProgress = syncing;
      
      cloudStatusEl.classList.remove('enabled', 'disabled', 'syncing');
      ledEl.classList.remove('green', 'gray', 'blue');
      
      if (syncing) {
        cloudStatusEl.classList.add('syncing');
        ledEl.classList.add('blue');
        textEl.innerHTML = 'Cloud<br>Syncing...';
      } else if (enabled) {
        cloudStatusEl.classList.add('enabled');
        ledEl.classList.add('green');
        textEl.innerHTML = `Cloud<br>${provider || 'Enabled'}`;
      } else {
        cloudStatusEl.classList.add('disabled');
        ledEl.classList.add('gray');
        textEl.innerHTML = 'Cloud<br>Disabled';
      }
      
      console.log('Cloud status:', syncing ? 'syncing' : (enabled ? 'enabled' : 'disabled'), 'provider:', provider);
    }

    function updateWebSocketStatus(connected) {
      const connectionStatusEl = document.getElementById('connection-status');
      const ledEl = connectionStatusEl.querySelector('.led-indicator');
      const textEl = connectionStatusEl.querySelector('.status-text');
      
      socketConnected = connected;
      
      connectionStatusEl.classList.remove('connected', 'disconnected');
      ledEl.classList.remove('green', 'red');
      
      if (connected) {
        connectionStatusEl.classList.add('connected');
        ledEl.classList.add('green');
        textEl.innerHTML = 'WebSocket<br>Connected';
      } else {
        connectionStatusEl.classList.add('disconnected');
        ledEl.classList.add('red');
        textEl.innerHTML = 'WebSocket<br>Disconnected';
      }
    }

    function handleNetworkStatusUpdate(data) {
      console.log('Network status update:', data);
      
      if (typeof data.wifi_connected !== 'undefined') {
        wifiConnected = data.wifi_connected;
      }
      
      if (typeof data.internet_connected !== 'undefined') {
        updateInternetStatus(data.internet_connected, false);
      }
      
      if (typeof data.cloud_enabled !== 'undefined') {
        updateCloudStatus(data.cloud_enabled, data.cloud_sync_in_progress || false);
      }
    }

    function checkInternetConnectivity() {
      const now = Date.now();
      if (now - lastInternetCheck < 5000) return;
      
      lastInternetCheck = now;
      updateInternetStatus(internetConnected, true);
      
      console.log('Checking internet connectivity...');
      
      fetch('/internet_status', {
        method: 'GET',
        cache: 'no-cache',
        signal: AbortSignal.timeout(8000)
      })
      .then(response => {
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        return response.json();
      })
      .then(data => {
        console.log('Internet status response:', data);
        
        wifiConnected = data.wifi_connected || false;
        const newInternetStatus = data.internet_connected || false;
        
        updateInternetStatus(newInternetStatus, false);
        
        if (typeof data.cloud_enabled !== 'undefined') {
          updateCloudStatus(data.cloud_enabled, data.cloud_sync_in_progress || false);
        }
        
        if (data.wifi_ssid) {
          console.log(`WiFi: ${data.wifi_ssid} (${data.wifi_rssi}dBm)`);
        }
      })
      .catch(error => {
        console.warn('Internet check failed:', error.message);
        updateInternetStatus(false, false);
      });
    }

    function startInternetMonitoring() {
      console.log('Starting internet and cloud connectivity monitoring...');
      
      setTimeout(checkInternetConnectivity, 2000);
      
      internetCheckInterval = setInterval(checkInternetConnectivity, INTERNET_CHECK_FREQUENCY);
    }

    function stopInternetMonitoring() {
      if (internetCheckInterval) {
        clearInterval(internetCheckInterval);
        internetCheckInterval = null;
        console.log('Stopped internet monitoring');
      }
    }

    // =============================================
    // STATE VISUALIZATION FUNCTIONS
    // =============================================
    function updateStateVisualization(state) {
      if (state === lastKnownState) return;
      lastKnownState = state;
      
      const container = document.getElementById('image-container');
      const indicator = document.getElementById('state-indicator');
      const recoilImage = document.getElementById('recoil-image');
      const compressionImage = document.getElementById('compression-image');
      
      container.classList.remove('compression', 'recoil', 'quietude');
      indicator.classList.remove('compression', 'recoil', 'quietude');
      recoilImage.classList.remove('active');
      compressionImage.classList.remove('active');
      
      container.classList.add(state);
      indicator.classList.add(state);
      
      if (state === 'compression') {
        compressionImage.classList.add('active');
        indicator.textContent = 'Compression';
      } else {
        recoilImage.classList.add('active');
        indicator.textContent = state === 'recoil' ? 'Recoil' : 'Quietude';
      }
    }

    function handleImageLoad(imageType) {
      imagesLoaded[imageType] = true;
      console.log(`${imageType} image loaded`);
      
      if (imagesLoaded.recoil && imagesLoaded.compression) {
        allImagesReady = true;
        console.log('All images loaded successfully');
      }
    }

    function handleImageError(imageType, src) {
      console.error(`Failed to load ${imageType} image: ${src}`);
      imagesLoaded[imageType] = false;
    }

    // =============================================
    // CRITICAL FIX: ROBUST RECORDING CONTROL
    // =============================================
    function toggleRecording() {
      // Prevent multiple simultaneous requests
      if (recordingActionInProgress) {
        console.log('Recording action already in progress, ignoring click');
        return;
      }
      
      console.log(`TOGGLE RECORDING: Current state: ${isRecording}, Session: ${currentSessionId}`);
      
      // Initialize audio on first recording start
      if (!isRecording && !audioInitialized) {
        console.log('Enabling audio for MP3 playback...');
        
        const audioStatus = document.getElementById('audio-status');
        audioStatus.classList.add('show');
        audioStatus.textContent = 'Enabling audio...';
        
        audioEnabled = true;
        audioInitialized = true;
        
        console.log('Audio enabled for MP3 playback');
        audioStatus.textContent = 'Audio enabled';
        
        setTimeout(() => {
          audioStatus.classList.remove('show');
        }, 2000);
      }
      
      startRecordingRequest();
    }

    function startRecordingRequest() {
      recordingActionInProgress = true;
      
      // Disable button and show loading state
      const recordButton = document.getElementById('record-button');
      const statusElement = document.getElementById('recording-status');
      
      if (recordButton) {
        recordButton.disabled = true;
        const originalText = recordButton.textContent;
        recordButton.textContent = isRecording ? 'Stopping...' : 'Starting...';
        
        // Reset button text after timeout as safety measure
        setTimeout(() => {
          if (recordButton.disabled) {
            recordButton.textContent = originalText;
            recordButton.disabled = false;
            recordingActionInProgress = false;
            console.warn('Recording request timeout - resetting button');
          }
        }, 10000); // 10 second timeout
      }
      
      if (statusElement) {
        statusElement.textContent = isRecording ? 'Stopping session...' : 'Starting session...';
      }
      
      console.log(`Sending recording request: ${isRecording ? 'STOP' : 'START'}`);
      
      fetch('/start_stop', { 
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        cache: 'no-cache'
      })
      .then(response => {
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        return response.json();
      })
      .then(data => {
        console.log('Recording toggle response:', data);
        
        // Process the response through the standard update function
        if (data.is_recording !== undefined) {
          updateRecordingStatus({
            type: 'recording_status',
            is_recording: data.is_recording,
            session_id: data.session_id,
            status: data.status
          });
        } else {
          console.warn('Invalid response format:', data);
          throw new Error('Invalid server response format');
        }
      })
      .catch(error => {
        console.error('Recording request failed:', error);
        
        // Reset UI on error
        recordingActionInProgress = false;
        
        if (recordButton) {
          recordButton.disabled = false;
          recordButton.textContent = isRecording ? 'Stop Training' : 'Start Training';
          recordButton.className = isRecording ? 'stop' : '';
        }
        
        if (statusElement) {
          statusElement.textContent = 'Error: ' + error.message;
          statusElement.style.color = '#f44336';
          
          // Reset status after delay
          setTimeout(() => {
            statusElement.textContent = isRecording ? `Recording Session ${currentSessionId}` : 'Ready to start training session';
            statusElement.style.color = isRecording ? '#f44336' : '#4CAF50';
          }, 3000);
        }
        
        // Show user-friendly error in alert box
        const alertBox = document.getElementById('alert-box');
        if (alertBox) {
          alertBox.className = 'alert-red';
          alertBox.textContent = 'Failed to toggle recording - check connection';
          
          setTimeout(() => {
            if (isRecording) {
              alertBox.className = 'alert-green';
              alertBox.textContent = 'Recording in progress';
            } else {
              alertBox.className = 'alert-gray';
              alertBox.textContent = 'Ready to start training session';
            }
          }, 5000);
        }
      });
    }

    // =============================================
    // MENU MANAGEMENT
    // =============================================
    function toggleMenu() {
      document.getElementById("dropdown-menu").classList.toggle("show");
    }

    window.onclick = function(event) {
      if (!event.target.closest('.menu-button') && !event.target.closest('.dropdown-menu')) {
        document.getElementById("dropdown-menu").classList.remove("show");
      }
    }

    // =============================================
    // FIXED WEBSOCKET CONNECTIONS WITH PROPER RECONNECTION
    // =============================================
    function connectAnimWebSocket() {
      if (connectingAnimSocket) {
        console.log('Animation WebSocket connection already in progress');
        return;
      }
      
      if (animSocket && animSocket.readyState === WebSocket.OPEN) {
        console.log('Animation WebSocket already connected');
        return;
      }
      
      connectingAnimSocket = true;
      
      try {
        // Close existing connection if any
        if (animSocket) {
          animSocket.onclose = null;
          animSocket.onerror = null;
          animSocket.close();
        }
        
        console.log('Connecting Animation WebSocket...');
        animSocket = new WebSocket(`ws://${window.location.hostname}/animws`);
        
        animSocket.onopen = function() {
          console.log('Animation WebSocket connected (20Hz)');
          animSocketConnected = true;
          animReconnectAttempts = 0;
          connectingAnimSocket = false;
        };

        animSocket.onmessage = function(event) {
          try {
            if (event.data instanceof Blob) {
              const reader = new FileReader();
              reader.onload = function() {
                const compressionState = new Uint8Array(reader.result)[0];
                const state = compressionState === 1 ? 'compression' : 'quietude';
                updateStateVisualization(state);
              };
              reader.readAsArrayBuffer(event.data);
            } else if (event.data instanceof ArrayBuffer) {
              const compressionState = new Uint8Array(event.data)[0];
              const state = compressionState === 1 ? 'compression' : 'quietude';
              updateStateVisualization(state);
            } else {
              try {
                const data = JSON.parse(event.data);
                if (data.state) {
                  updateStateVisualization(data.state);
                }
              } catch (e) {
                console.warn('Unexpected animation message format:', event.data);
              }
            }
          } catch (error) {
            console.error('Animation message processing error:', error);
          }
        };

        animSocket.onclose = function(event) {
          console.warn('Animation WebSocket disconnected, code:', event.code);
          animSocketConnected = false;
          connectingAnimSocket = false;
          
          if (animReconnectAttempts < maxReconnectAttempts) {
            animReconnectAttempts++;
            const delay = reconnectDelay * animReconnectAttempts;
            console.log(`Attempting animation reconnection ${animReconnectAttempts}/${maxReconnectAttempts} in ${delay}ms`);
            setTimeout(connectAnimWebSocket, delay);
          } else {
            console.error('Animation WebSocket max reconnection attempts reached');
          }
        };

        animSocket.onerror = function(error) {
          console.error('Animation WebSocket error:', error);
          animSocketConnected = false;
          connectingAnimSocket = false;
        };
        
      } catch (error) {
        console.error('Failed to create animation WebSocket:', error);
        connectingAnimSocket = false;
        setTimeout(connectAnimWebSocket, reconnectDelay);
      }
    }

    function connectWebSocket() {
      if (connectingSocket) {
        console.log('Metrics WebSocket connection already in progress');
        return;
      }
      
      if (socket && socket.readyState === WebSocket.OPEN) {
        console.log('Metrics WebSocket already connected');
        return;
      }
      
      connectingSocket = true;
      
      try {
        // Close existing connection if any
        if (socket) {
          socket.onclose = null;
          socket.onerror = null;
          socket.close();
        }
        
        console.log('Connecting Metrics WebSocket...');
        socket = new WebSocket(`ws://${window.location.hostname}/ws`);
        
        socket.onopen = function() {
          console.log('Metrics WebSocket connected (2Hz JSON)');
          updateWebSocketStatus(true);
          reconnectAttempts = 0;
          connectingSocket = false;
        };

        socket.onmessage = function(event) {
          try {
            const data = JSON.parse(event.data);
            
            if (data.type === 'network_status') {
              handleNetworkStatusUpdate(data);
            } else {
              addToQueue(data);
            }
          } catch (error) {
            console.error('Error parsing Metrics WebSocket message:', error);
          }
        };

        socket.onclose = function(event) {
          console.warn('Metrics WebSocket disconnected, code:', event.code);
          updateWebSocketStatus(false);
          connectingSocket = false;
          
          const alertBox = document.getElementById("alert-box");
          if (alertBox) {
            alertBox.className = "alert-gray";
            alertBox.textContent = "Connection lost - attempting to reconnect...";
          }
          
          if (reconnectAttempts < maxReconnectAttempts) {
            reconnectAttempts++;
            const delay = reconnectDelay * reconnectAttempts;
            console.log(`Attempting metrics reconnection ${reconnectAttempts}/${maxReconnectAttempts} in ${delay}ms`);
            setTimeout(connectWebSocket, delay);
          } else {
            console.error('Metrics WebSocket max reconnection attempts reached');
          }
        };

        socket.onerror = function(error) {
          console.error('Metrics WebSocket error:', error);
          updateWebSocketStatus(false);
          connectingSocket = false;
        };
        
      } catch (error) {
        console.error('Failed to create Metrics WebSocket:', error);
        connectingSocket = false;
        setTimeout(connectWebSocket, reconnectDelay);
      }
    }

    // =============================================
    // ENHANCED INITIALIZATION WITH PROPER STATE SYNCHRONIZATION
    // =============================================
    function initializeDashboard() {
      console.log('CPR Dashboard initializing with enhanced state management...');
      
      // Initialize displays
      document.getElementById("session-id").textContent = '--';
      
      // Get initial server state
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          console.log('Initial server status:', data);
          
          // Synchronize client state with server
          if (typeof data.recording !== 'undefined') {
            updateRecordingStatus({
              type: 'recording_status',
              is_recording: data.recording,
              session_id: data.session_id || 0
            });
          }
          
          // Update session display
          if (data.next_session) {
            const sessionEl = document.getElementById('session-id');
            if (sessionEl && !isRecording) {
              sessionEl.textContent = data.next_session;
            }
          }
        })
        .catch(error => {
          console.warn('Failed to get initial server status:', error);
        });
      
      // Connect WebSockets
      setTimeout(() => {
        connectWebSocket();
        connectAnimWebSocket();
      }, 1000);
      
      // Start monitoring
      startInternetMonitoring();
      
      // Force image reload if needed
      setTimeout(() => {
        if (!allImagesReady) {
          console.warn('Images taking longer than expected, forcing reload...');
          const recoilImg = document.getElementById('recoil-image');
          const compressionImg = document.getElementById('compression-image');
          if (recoilImg) recoilImg.src = recoilImg.src + '?t=' + Date.now();
          if (compressionImg) compressionImg.src = compressionImg.src + '?t=' + Date.now();
        }
      }, 3000);
      
      console.log('Dashboard initialization complete');
    }

    // =============================================
    // ENHANCED PAGE LOAD EVENT
    // =============================================
    window.addEventListener('load', function() {
      initializeDashboard();
      
      // Cleanup on page unload
      window.addEventListener('beforeunload', () => {
        stopInternetMonitoring();
        stopAudioInterval();
        
        // Close WebSocket connections cleanly
        if (socket) {
          socket.onclose = null;
          socket.close();
        }
        if (animSocket) {
          animSocket.onclose = null;
          animSocket.close();
        }
      });
    });

    // =============================================
    // DEBUG FUNCTIONS AND ENHANCED GLOBAL OBJECT
    // =============================================
    window.cprDashboard = {
      getStatus: function() {
        return {
          isRecording: isRecording,
          currentSessionId: currentSessionId,
          lastKnownState: lastKnownState,
          recordingActionInProgress: recordingActionInProgress,
          audioEnabled: audioEnabled,
          audioInitialized: audioInitialized,
          isPlayingAudio: isPlayingAudio,
          internetConnected: internetConnected,
          wifiConnected: wifiConnected,
          cloudEnabled: cloudEnabled,
          cloudSyncInProgress: cloudSyncInProgress,
          socketConnected: socketConnected,
          animSocketConnected: animSocketConnected,
          connectingSocket: connectingSocket,
          connectingAnimSocket: connectingAnimSocket,
          webSocketConnected: socket && socket.readyState === WebSocket.OPEN,
          animWebSocketConnected: animSocket && animSocket.readyState === WebSocket.OPEN,
          allImagesReady: allImagesReady,
          currentAlerts: currentAlerts,
          messageQueueLength: messageQueue.length,
          reconnectAttempts: reconnectAttempts,
          animReconnectAttempts: animReconnectAttempts
        };
      },

      forceReconnectWebSockets: function() {
        console.log('Force reconnecting WebSockets...');
        reconnectAttempts = 0;
        animReconnectAttempts = 0;
        connectingSocket = false;
        connectingAnimSocket = false;
        
        if (socket) {
          socket.onclose = null;
          socket.close();
        }
        if (animSocket) {
          animSocket.onclose = null;
          animSocket.close();
        }
        
        setTimeout(() => {
          connectWebSocket();
          connectAnimWebSocket();
        }, 1000);
      },
      
      syncWithServer: function() {
        console.log('Syncing with server state...');
        fetch('/status')
          .then(response => response.json())
          .then(data => {
            console.log('Server sync response:', data);
            if (typeof data.recording !== 'undefined') {
              updateRecordingStatus({
                type: 'recording_status',
                is_recording: data.recording,
                session_id: data.session_id || 0
              });
            }
          })
          .catch(error => {
            console.error('Server sync failed:', error);
          });
      },
      
      testRecordingToggle: function() {
        console.log('Testing recording toggle...');
        if (!recordingActionInProgress) {
          toggleRecording();
        } else {
          console.log('Recording action already in progress');
        }
      },
      
      testAudio: function(alertText = "Press harder") {
        console.log('Testing audio with alert:', alertText);
        if (!audioEnabled) {
          console.warn('Audio not enabled');
          return false;
        }
        
        for (const [text, audioType] of Object.entries(alertToAudioMap)) {
          if (alertText.includes(text)) {
            playAudio(audioType);
            return true;
          }
        }
        
        console.warn('No audio mapping found for:', alertText);
        return false;
      },
      
      testAllAlerts: function() {
        if (!audioEnabled) {
          console.warn('Audio not enabled');
          return;
        }
        
        const alertTypes = Object.keys(audioFiles);
        let index = 0;
        
        const playNext = () => {
          if (index < alertTypes.length) {
            console.log(`Testing ${index + 1}/${alertTypes.length}: ${alertTypes[index]}`);
            playAudio(alertTypes[index]);
            index++;
            setTimeout(playNext, AUDIO_GAP + 500);
          } else {
            console.log('All audio tests completed');
          }
        };
        
        playNext();
      },
      
      testAnimation: function() {
        const states = ['compression', 'recoil', 'quietude'];
        let index = 0;
        
        const cycleState = () => {
          updateStateVisualization(states[index % states.length]);
          index++;
          if (index < 9) {
            setTimeout(cycleState, 1000);
          }
        };
        
        cycleState();
      },
      
      testInternetConnectivity: function() {
        checkInternetConnectivity();
      },
      
      forceInternetCheck: function() {
        lastInternetCheck = 0;
        checkInternetConnectivity();
      },
      
      enableAudio: function() {
        return initAudio();
      },
      
      disableAudio: function() {
        audioEnabled = false;
        stopAudioInterval();
        console.log('Audio disabled');
      },
      
      resetRecordingState: function() {
        console.log('Resetting recording state...');
        recordingActionInProgress = false;
        const recordButton = document.getElementById('record-button');
        if (recordButton) {
          recordButton.disabled = false;
          recordButton.textContent = isRecording ? 'Stop Training' : 'Start Training';
          recordButton.className = isRecording ? 'stop' : '';
        }
      }
    };

    // Show enhanced helpful tips in console
    console.log('CPR Dashboard Debug Commands (Fixed Version):');
    console.log('- cprDashboard.getStatus() - Complete system status');
    console.log('- cprDashboard.forceReconnectWebSockets() - Force WebSocket reconnection');
    console.log('- cprDashboard.syncWithServer() - Sync state with server');
    console.log('- cprDashboard.testRecordingToggle() - Test recording toggle');
    console.log('- cprDashboard.resetRecordingState() - Reset recording UI state');
    console.log('- cprDashboard.testAudio("Press harder") - Test audio');
    console.log('- cprDashboard.testAllAlerts() - Test all alert types');
    console.log('- cprDashboard.testAnimation() - Test animation states');
    console.log('- cprDashboard.testInternetConnectivity() - Test internet');
    console.log('Cloud Configuration: /cloud_config');
    console.log('WiFi Configuration: /ssid_config');
    console.log('System Debug: /debug');
    console.log('WebSocket Endpoints: /ws (metrics), /animws (animation)');
    console.log('Available MP3 files:', Object.values(audioFiles));