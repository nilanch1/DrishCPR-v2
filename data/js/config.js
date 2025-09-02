// Hamburger Menu Functions
    function toggleMenu() {
      document.getElementById("menuContent").classList.toggle("show");
    }

    window.onclick = function(event) {
      if (!event.target.matches('.menu-btn')) {
        const menus = document.getElementsByClassName("menu-content");
        for (let i = 0; i < menus.length; i++) {
          if (menus[i].classList.contains('show')) {
            menus[i].classList.remove('show');
          }
        }
      }
    }

    // Initialize WebSocket connection - FIXED FOR ESP32
    const socket = new WebSocket(`ws://${window.location.hostname}/ws`);
    
    // Listen for configuration updates
    socket.onmessage = function(event) {
      const data = JSON.parse(event.data);
      if (data.type === 'config_updated') {
        console.log('Configuration updated:', data);
        showStatus(data.message || 'Configuration updated successfully!', 'success');
      }
    };

    function showStatus(message, type = 'info') {
      const statusEl = document.getElementById('status');
      statusEl.textContent = message;
      statusEl.className = `status-${type}`;
      
      // Clear status after 3 seconds
      setTimeout(() => {
        statusEl.textContent = '';
        statusEl.className = '';
      }, 3000);
    }

    // Load current configuration
    document.getElementById('load').addEventListener('click', async () => {
      try {
        showStatus('Loading current configuration...', 'info');
        const response = await fetch('/get_config');
        
        if (response.ok) {
          const data = await response.json();
          const config = data.config;
          
          // Update form fields with current config
          Object.keys(config).forEach(key => {
            const input = document.querySelector(`input[name="${key}"]`);
            if (input) {
              input.value = config[key];
            }
          });
          
          showStatus('Configuration loaded successfully!', 'success');
        } else {
          throw new Error('Failed to load configuration');
        }
      } catch (err) {
        console.error('Load error:', err);
        showStatus('Error loading configuration', 'error');
      }
    });

    // Save configuration
    document.getElementById('save').addEventListener('click', async () => {
      const formData = new FormData(document.getElementById('configForm'));
      const params = Object.fromEntries(formData.entries());
      
      // Convert numeric values
      const numericFields = ['r1', 'r2', 'c1', 'c2', 'f1', 'f2', 'n', 'smoothing_window'];
      numericFields.forEach(field => {
        if (params[field]) params[field] = parseInt(params[field]);
      });
      
      const floatFields = ['quiet_threshold', 'rate_smoothing_factor', 'compression_grace_period'];
      floatFields.forEach(field => {
        if (params[field]) params[field] = parseFloat(params[field]);
      });
      
      try {
        showStatus('Saving configuration...', 'info');
        
        const response = await fetch('/config', {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json'
          },
          body: JSON.stringify(params)
        });
        
        if (response.ok) {
          const result = await response.json();
          console.log('Save successful:', result);
          showStatus('Configuration saved and applied successfully!', 'success');
        } else {
          const error = await response.json();
          throw new Error(error.error || 'Failed to save');
        }
      } catch (err) {
        console.error('Save error:', err);
        showStatus(`Error saving configuration: ${err.message}`, 'error');
      }
    });

    // Load current configuration on page load
    window.addEventListener('load', () => {
      document.getElementById('load').click();
    });