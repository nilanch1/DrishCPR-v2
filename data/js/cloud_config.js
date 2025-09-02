function toggleMenu() {
      document.getElementById("menuContent").classList.toggle("show");
    }
    window.onclick = function(event) {
      if (!event.target.matches('.menu-btn')) {
        const menus = document.getElementsByClassName("menu-content");
        for (let i = 0; i < menus.length; i++) {
          menus[i].classList.remove('show');
        }
      }
    }

    function showStatus(message, type='info') {
      const el=document.getElementById("status");
      el.textContent=message;
      el.className=`status-${type}`;
      setTimeout(()=>{el.textContent='';el.className='';},3000);
    }
    function saveCloudConfig() {
  const params = Object.fromEntries(new FormData(document.getElementById("cloudForm")).entries());
  console.log("Saving cloud config:", params); // Debug log
  
  const jsonBody = JSON.stringify(params);
  console.log('JSON being sent:', jsonBody);
  fetch('/save_cloud_config', {
    
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(params)
  })
  .then(r => r.json())
  .then(d => {
    console.log("Save response:", d); // Debug log
    if (d.success) {
      showStatus("Cloud configuration saved!", "success");
    } else {
      showStatus(`Error: ${d.error}`, "error");
    }
  })
  .catch(e => {
    console.error("Save error:", e);
    showStatus(`Network error: ${e}`, "error");
  });
}
    function testConnection() {
      const params=Object.fromEntries(new FormData(document.getElementById("cloudForm")).entries());
      showStatus("Testing connection...","info");
      fetch('/test_cloud_connection',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(params)})
      .then(r=>r.json()).then(d=>d.success?showStatus("Connection test successful!","success"):showStatus(`Connection failed: ${d.error}`,"error"))
      .catch(e=>showStatus(`Network error: ${e}`,"error"));
    }

    function loadSavedConfig() {
  showStatus("Loading saved config...", "info");
  fetch('/get_cloud_config')
  .then(r => r.json())
  .then(d => {
    console.log("Cloud config response:", d); // Debug log
    
    if (d.success || d.provider) { // Check if we have valid config data
      // Set provider
      if (d.provider) {
        document.querySelector('[name="provider"]').value = d.provider;
      }
      
      // Set credentials (handle both field names)
      document.getElementById('access_key').value = d.access_key || d.accessKey || '';
      document.getElementById('secret_key').value = d.secret_key || d.secretKey || '';
      
      // Set bucket and endpoint
      document.querySelector('[name="bucket"]').value = d.bucket || d.bucketName || '';
      document.querySelector('[name="endpoint"]').value = d.endpoint || d.endpointUrl || '';
      
      // Set frequency - handle both field names and ensure it's a valid number
      const frequency = d.frequency || d.syncFrequency;
      if (typeof frequency === 'number' && frequency >= 5) {
        document.querySelector('[name="frequency"]').value = frequency;
      } else {
        document.querySelector('[name="frequency"]').value = 60; // Default
      }
      
      showStatus("Config loaded successfully", "success");
    } else {
      showStatus("No saved configuration found", "error");
    }
  })
  .catch(e => {
    console.error("Error loading config:", e);
    showStatus(`Network error: ${e}`, "error");
  });
}
  function revealKey(id) {
    const field = document.getElementById(id);
    field.type = "text";   // show the value
    setTimeout(()=>{ field.type = "password"; }, 3000); // hide again after 3s
  }

    // Internet LED updater
    function updateInternetLED() {
      fetch('/internet_status')
      .then(r=>r.json())
      .then(d=>{
        const led=document.getElementById("internet-led");
        if(d.internet_connected){
          led.style.background="limegreen";
        } else {
          led.style.background="red";
        }
      }).catch(_=>{
        document.getElementById("internet-led").style.background="gray";
      });
    }
    setInterval(updateInternetLED,5000);
    updateInternetLED();