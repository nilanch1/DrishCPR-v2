// =============================================
    // WIFI CONFIGURATION MODULE (NAMESPACED)
    // =============================================
    const WiFiConfig = (function() {
      // Private variables
      let menuOpen = false;
      // let isScanning = false;
      let isConnecting = false;

      // =============================================
      // UTILITY FUNCTIONS
      // =============================================
      function showStatus(message, type = 'info', duration = 5000) {
        const statusEl = document.getElementById("statusMessage");
        
        // Remove all status classes
        statusEl.classList.remove('status-success', 'status-error', 'status-info', 'status-warning');
        
        // Add appropriate class and message
        statusEl.classList.add(`status-${type}`);
        statusEl.textContent = message;
        statusEl.style.display = 'block';
        
        console.log(`📱 Status [${type}]: ${message}`);
        
        // Auto-hide after duration (unless it's a loading message)
        if (duration > 0 && !message.includes('...')) {
          setTimeout(() => {
            statusEl.style.display = 'none';
          }, duration);
        }
      }

      // function getSignalBars(rssi) {
      //   if (rssi >= -50) return '📶📶📶📶'; // Excellent
      //   if (rssi >= -60) return '📶📶📶'; // Good
      //   if (rssi >= -70) return '📶📶'; // Fair
      //   if (rssi >= -80) return '📶'; // Poor
      //   return '📵'; // Very poor
      // }

      function resetButtons() {
        isConnecting = false;
        
        const saveBtn = document.getElementById('saveBtn');
        const testBtn = document.getElementById('testBtn');
        
        saveBtn.disabled = false;
        testBtn.disabled = false;
        saveBtn.innerHTML = '💾 Save & Connect';
      }

      // =============================================
      // MENU FUNCTIONS
      // =============================================
      function toggleMenu() {
        const menuContent = document.getElementById("menuContent");
        const menuBtn = document.getElementById("menuBtn");
        
        menuOpen = !menuOpen;
        
        if (menuOpen) {
          menuContent.classList.add("show");
          menuBtn.classList.add("active");
        } else {
          menuContent.classList.remove("show");
          menuBtn.classList.remove("active");
        }
        
        console.log('📱 Menu toggled:', menuOpen ? 'open' : 'closed');
      }

      // =============================================
      // NETWORK STATUS FUNCTIONS
      // =============================================
      function updateNetworkStatus() {
        showStatus("Checking network status...", "info", 0);
        
        fetch('/network_status', {
          method: 'GET',
          cache: 'no-cache'
        })
        .then(response => {
          if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
          }
          return response.json();
        })
        .then(data => {
          console.log('📡 Network status:', data);
          
          // Update status display
          document.getElementById('connectionStatus').textContent = 
            data.wifi_connected ? '✅ Connected' : '❌ Disconnected';
          
          document.getElementById('currentSSID').textContent = 
            data.wifi_ssid || '--';
          
          document.getElementById('signalStrength').textContent = 
            data.wifi_rssi ? `${data.wifi_rssi} dBm` : '--';
          
          document.getElementById('ipAddress').textContent = 
            data.ip_address || '--';
          
          if (data.wifi_connected) {
            showStatus(`✅ Connected to "${data.wifi_ssid}" with signal ${data.wifi_rssi} dBm`, "success");
          } else {
            showStatus("❌ Not connected to WiFi", "warning");
          }
        })
        .catch(error => {
          console.error('📡 Network status error:', error);
          showStatus(`❌ Error checking network status: ${error.message}`, "error");
          
          // Set default values
          document.getElementById('connectionStatus').textContent = 'Error';
          document.getElementById('currentSSID').textContent = '--';
          document.getElementById('signalStrength').textContent = '--';
          document.getElementById('ipAddress').textContent = '--';
        });
      }

      // =============================================
      // NETWORK SCANNING FUNCTIONS
      // =============================================
      // function scanNetworks() {
      //   if (isScanning) {
      //     console.log('📡 Scan already in progress');
      //     return;
      //   }
        
      //   isScanning = true;
      //   const scanBtn = document.getElementById('scanBtn');
      //   const networkList = document.getElementById('networkList');
        
      //   // Update button state
      //   scanBtn.disabled = true;
      //   scanBtn.innerHTML = '<span class="loading"></span>Scanning...';
        
      //   // Show scanning message
      //   networkList.innerHTML = '<div style="text-align: center; color: #2196F3; padding: 20px;"><span class="loading"></span>Scanning for networks...</div>';
        
      //   showStatus("🔍 Scanning for available WiFi networks...", "info", 0);
        
      //   fetch('/scan_networks', {
      //     method: 'POST',
      //     headers: {
      //       'Content-Type': 'application/json'
      //     },
      //     cache: 'no-cache'
      //   })
      //   .then(response => {
      //     if (!response.ok) {
      //       throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      //     }
      //     return response.json();
      //   })
      //   .then(data => {
      //     console.log('📡 Scan results:', data);
          
      //     if (data.success && data.networks && data.networks.length > 0) {
      //       displayNetworks(data.networks);
      //       showStatus(`✅ Found ${data.networks.length} networks`, "success");
      //     } else {
      //       networkList.innerHTML = '<div style="text-align: center; color: #f44336; padding: 20px;">❌ No networks found</div>';
      //       showStatus("❌ No networks found", "warning");
      //     }
      //   })
      //   .catch(error => {
      //     console.error('📡 Scan error:', error);
      //     networkList.innerHTML = '<div style="text-align: center; color: #f44336; padding: 20px;">❌ Scan failed</div>';
      //     showStatus(`❌ Scan failed: ${error.message}`, "error");
      //   })
      //   .finally(() => {
      //     // Reset button state
      //     isScanning = false;
      //     scanBtn.disabled = false;
      //     scanBtn.innerHTML = '🔍 Scan for Networks';
      //   });
      // }

      // function displayNetworks(networks) {
      //   const networkList = document.getElementById('networkList');
        
      //   if (networks.length === 0) {
      //     networkList.innerHTML = '<div style="text-align: center; color: #f44336; padding: 20px;">No networks available</div>';
      //     return;
      //   }
        
      //   // Sort networks by signal strength (higher RSSI = stronger signal)
      //   networks.sort((a, b) => (b.rssi || -100) - (a.rssi || -100));
        
      //   let html = '';
      //   networks.forEach(network => {
      //     const signalStrength = network.rssi || -100;
      //     const signalBars = getSignalBars(signalStrength);
      //     const security = network.auth_mode > 0 ? '🔒' : '🔓';
          
      //     html += `
      //       <div class="network-item-list" onclick="WiFiConfig.selectNetwork('${network.ssid}', ${network.auth_mode})">
      //         <div>
      //           <div class="network-name">${security} ${network.ssid}</div>
      //           <div style="font-size: 0.8rem; color: #B0BEC5;">
      //             ${network.auth_mode > 0 ? 'Secured' : 'Open'} • Channel ${network.channel || 'Unknown'}
      //           </div>
      //         </div>
      //         <div class="network-signal">
      //           ${signalBars} ${signalStrength} dBm
      //         </div>
      //       </div>
      //     `;
      //   });
        
      //   networkList.innerHTML = html;
      // }

      // function selectNetwork(ssid, authMode) {
      //   console.log(`📡 Selected network: ${ssid} (auth: ${authMode})`);
        
      //   document.getElementById('ssid').value = ssid;
        
      //   // Focus on password field if network is secured
      //   if (authMode > 0) {
      //     document.getElementById('password').focus();
      //     showStatus(`🔐 Enter password for "${ssid}"`, "info");
      //   } else {
      //     showStatus(`✅ Selected open network "${ssid}"`, "success");
      //   }
      // }

      // =============================================
      // CONNECTION FUNCTIONS
      // =============================================
      function saveWifiConfig() {
        if (isConnecting) {
          console.log('📡 Connection already in progress');
          return;
        }
        
        const ssid = document.getElementById('ssid').value.trim();
        const password = document.getElementById('password').value;
        
        if (!ssid) {
          showStatus("❌ Please enter a WiFi network name", "error");
          document.getElementById('ssid').focus();
          return;
        }
        
        isConnecting = true;
        const saveBtn = document.getElementById('saveBtn');
        const testBtn = document.getElementById('testBtn');
        
        // Update button states
        saveBtn.disabled = true;
        testBtn.disabled = true;
        saveBtn.innerHTML = '<span class="loading"></span>Connecting...';
        
        showStatus("💾 Saving WiFi configuration and connecting...", "info", 0);
        
        const wifiConfig = {
          ssid: ssid,
          password: password
        };
        
        fetch('/save_wifi_config', {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json'
          },
          body: JSON.stringify(wifiConfig)
        })
        .then(response => {
          if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
          }
          return response.json();
        })
        .then(data => {
          console.log('📡 WiFi config response:', data);
          
          if (data.success) {
            showStatus("✅ WiFi configuration saved! Attempting to connect...", "success");
            
            // Start monitoring connection status
            setTimeout(() => {
              monitorConnection(ssid);
            }, 3000);
            
          } else {
            throw new Error(data.error || 'Unknown error occurred');
          }
        })
        .catch(error => {
          console.error('📡 WiFi config error:', error);
          showStatus(`❌ Error saving WiFi config: ${error.message}`, "error");
          
          // Reset button states
          resetButtons();
        });
      }

      function monitorConnection(expectedSSID, attempts = 0) {
        const maxAttempts = 10; // 30 seconds total
        
        if (attempts >= maxAttempts) {
          showStatus(`❌ Connection timeout. Please check credentials and try again.`, "error");
          resetButtons();
          return;
        }
        
        showStatus(`🔄 Checking connection... (${attempts + 1}/${maxAttempts})`, "info", 0);
        
        fetch('/network_status', {
          method: 'GET',
          cache: 'no-cache'
        })
        .then(response => response.json())
        .then(data => {
          console.log(`📡 Connection check ${attempts + 1}:`, data);
          
          if (data.wifi_connected && data.wifi_ssid === expectedSSID) {
            // Success!
            showStatus(`🎉 Successfully connected to "${expectedSSID}"! Signal: ${data.wifi_rssi} dBm`, "success");
            updateNetworkStatus();
            resetButtons();
          } else if (data.wifi_connected && data.wifi_ssid !== expectedSSID) {
            // Connected to different network
            showStatus(`⚠️ Connected to "${data.wifi_ssid}" instead of "${expectedSSID}"`, "warning");
            updateNetworkStatus();
            resetButtons();
          } else {
            // Still trying to connect
            setTimeout(() => {
              monitorConnection(expectedSSID, attempts + 1);
            }, 3000);
          }
        })
        .catch(error => {
          console.error(`📡 Connection check ${attempts + 1} error:`, error);
          setTimeout(() => {
            monitorConnection(expectedSSID, attempts + 1);
          }, 3000);
        });
      }

      function testConnection() {
        const ssid = document.getElementById('ssid').value.trim();
        const password = document.getElementById('password').value;
        
        if (!ssid) {
          showStatus("❌ Please enter a WiFi network name", "error");
          document.getElementById('ssid').focus();
          return;
        }
        
        showStatus("🔗 Testing WiFi connection...", "info", 0);
        
        const testBtn = document.getElementById('testBtn');
        testBtn.disabled = true;
        testBtn.innerHTML = '<span class="loading"></span>Testing...';
        
        const wifiConfig = {
          ssid: ssid,
          password: password
        };
        
        fetch('/test_wifi_connection', {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json'
          },
          body: JSON.stringify(wifiConfig)
        })
        .then(response => response.json())
        .then(data => {
          if (data.success) {
            showStatus(`✅ Connection test successful! Signal: ${data.rssi} dBm`, "success");
          } else {
            showStatus(`❌ Connection test failed: ${data.error}`, "error");
          }
        })
        .catch(error => {
          console.error('📡 Test connection error:', error);
          showStatus(`❌ Test failed: ${error.message}`, "error");
        })
        .finally(() => {
          testBtn.disabled = false;
          testBtn.innerHTML = '🔗 Test Connection';
        });
      }

      // =============================================
      // INITIALIZATION
      // =============================================
      function init() {
        console.log('📱 WiFi Configuration module initialized');
        
        // Load initial network status
        updateNetworkStatus();
        
        // Load existing WiFi config if available
        fetch('/get_wifi_config')
          .then(response => {
            if (response.ok) {
              return response.json();
            }
            throw new Error('No existing config');
          })
          .then(data => {
            if (data.ssid) {
              document.getElementById('ssid').value = data.ssid;
              console.log('📡 Loaded existing SSID:', data.ssid);
            }
          })
          .catch(error => {
            console.log('ℹ️ No existing WiFi config found');
          });

        // Set up event listeners
        setupEventListeners();
      }

      function setupEventListeners() {
        // Close menu when clicking outside
        document.addEventListener('click', function(event) {
          const hamburgerMenu = document.querySelector('.hamburger-menu');
          
          if (!hamburgerMenu.contains(event.target) && menuOpen) {
            toggleMenu();
          }
        });

        // Prevent menu close when clicking inside menu
        document.getElementById("menuContent").addEventListener('click', function(event) {
          event.stopPropagation();
        });

        // Form submission handling
        document.getElementById('wifiForm').addEventListener('submit', function(e) {
          e.preventDefault();
          saveWifiConfig();
        });

        // Enter key handling for password field
        document.getElementById('password').addEventListener('keypress', function(e) {
          if (e.key === 'Enter') {
            saveWifiConfig();
          }
        });
      }

      // =============================================
      // PUBLIC API
      // =============================================
      return {
        // Menu functions
        toggleMenu: toggleMenu,
        
        // Network functions
        updateNetworkStatus: updateNetworkStatus,
        // scanNetworks: scanNetworks,
        // selectNetwork: selectNetwork,
        
        // Configuration functions
        saveWifiConfig: saveWifiConfig,
        testConnection: testConnection,
        
        // Utility functions
        resetButtons: resetButtons,
        
        // Initialization
        init: init,
        
        // Status for debugging
        getStatus: function() {
          return {
            menuOpen: menuOpen,
            // isScanning: isScanning,
            isConnecting: isConnecting
          };
        }
      };
    })();

    // =============================================
    // GLOBAL INITIALIZATION
    // =============================================
    document.addEventListener('DOMContentLoaded', function() {
      console.log('📱 WiFi Configuration page loaded');
      WiFiConfig.init();
    });

    // =============================================
    // GLOBAL ERROR HANDLER
    // =============================================
    window.addEventListener('error', function(event) {
      console.error('📱 Global error:', event.error);
      
      const statusEl = document.getElementById("statusMessage");
      if (statusEl) {
        statusEl.classList.remove('status-success', 'status-error', 'status-info', 'status-warning');
        statusEl.classList.add('status-error');
        statusEl.textContent = 'An unexpected error occurred. Please refresh the page.';
        statusEl.style.display = 'block';
      }
    });

    // =============================================
    // DEBUG FUNCTIONS (GLOBAL SCOPE)
    // =============================================
    window.WiFiConfigDebug = {
      getModule: () => WiFiConfig,
      // testScan: () => WiFiConfig.scanNetworks(),
      testStatus: () => WiFiConfig.updateNetworkStatus(),
      getStatus: () => WiFiConfig.getStatus(),
      resetAll: () => {
        WiFiConfig.resetButtons();
        const statusEl = document.getElementById("statusMessage");
        statusEl.style.display = 'none';
      }
    };

    console.log('📱 WiFi Configuration Debug Commands:');
    // console.log('- WiFiConfigDebug.testScan() - Test network scanning');
    console.log('- WiFiConfigDebug.testStatus() - Test status update');
    console.log('- WiFiConfigDebug.getStatus() - Get current status');
    console.log('- WiFiConfigDebug.resetAll() - Reset all states');