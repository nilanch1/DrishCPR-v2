// =============================================
    // MENU FUNCTIONS
    // =============================================
    function toggleMenu() {
      document.getElementById("menuContent").classList.toggle("show");
    }

    window.onclick = function(event) {
      if (!event.target.matches('.menu-btn') && !event.target.closest('.menu-btn')) {
        const menus = document.getElementsByClassName("menu-content");
        for (let i = 0; i < menus.length; i++) {
          if (menus[i].classList.contains('show')) {
            menus[i].classList.remove('show');
          }
        }
      }
    }

    // =============================================
    // FIXED CSV MANAGEMENT FUNCTIONS - DIRECT FILE ACCESS
    // =============================================
    function downloadCSV() {
      console.log('downloadCSV() called');
      try {
        window.location.href = '/download_csv';
      } catch (error) {
        console.error('Download CSV error:', error);
        alert('Error downloading CSV: ' + error.message);
      }
    }

    function deleteCSV() {
      console.log('deleteCSV() called');
      
      if (confirm('Are you sure you want to delete the CSV file?\n\nThis action cannot be undone, but the session numbering will be preserved.')) {
        fetch('/delete_csv', { 
          method: 'POST',
          headers: {
            'Content-Type': 'application/json'
          }
        })
        .then(response => {
          console.log('Delete CSV response status:', response.status);
          return response.json();
        })
        .then(data => {
          console.log('Delete CSV response data:', data);
          if (data.success) {
            alert('CSV file deleted successfully!\n\nSession numbering has been preserved.');
            refreshData();
          } else {
            alert('Error: ' + data.error);
          }
        })
        .catch(error => {
          console.error('Delete CSV error:', error);
          alert('Network error: ' + error);
        });
      }
    }

    function viewCSVData() {
      console.log('viewCSVData() called - using direct file access');
      
      // Get the CSV filename from status
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          const chipId = data.chip_id;
          const csvFileName = data.csv_file_name;
          
          console.log('Chip ID:', chipId);
          console.log('CSV filename:', csvFileName);
          
          if (chipId) {
            // Use direct file access (what works!)
            const directUrl = `/${chipId}.csv`;
            console.log('Opening direct CSV URL:', directUrl);
            window.open(directUrl, '_blank');
          } else {
            alert('Error: Could not determine device chip ID');
          }
        })
        .catch(error => {
          console.error('Error getting status:', error);
          // Fallback - try with files API
          fetch('/files_api')
            .then(response => response.json())
            .then(data => {
              const chipId = data.chip_id;
              if (chipId) {
                const directUrl = `/${chipId}.csv`;
                console.log('Fallback: Opening direct CSV URL:', directUrl);
                window.open(directUrl, '_blank');
              } else {
                alert('Error: Could not determine CSV filename');
              }
            })
            .catch(fallbackError => {
              console.error('Fallback failed:', fallbackError);
              alert('Error: Could not access CSV file');
            });
        });
    }

    function viewCSVAsTable() {
      console.log('viewCSVAsTable() called - using custom table viewer');
      
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          const chipId = data.chip_id;
          
          if (chipId) {
            // Create a custom table viewer
            openCSVTableViewer(chipId);
          } else {
            alert('Error: Could not determine device chip ID');
          }
        })
        .catch(error => {
          console.error('Error getting status:', error);
          alert('Error: Could not access device status');
        });
    }

    function openCSVTableViewer(chipId) {
      const csvUrl = `/${chipId}.csv`;
      
      // Fetch the CSV content and create a nice table view
      fetch(csvUrl)
        .then(response => {
          if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
          }
          return response.text();
        })
        .then(csvContent => {
          createTableViewerWindow(csvContent, `${chipId}.csv`);
        })
        .catch(error => {
          console.error('Error fetching CSV:', error);
          alert('Error loading CSV file: ' + error.message);
        });
    }

    function createTableViewerWindow(csvContent, filename) {
      // Create a new window with formatted table
      const newWindow = window.open('', '_blank', 'width=1200,height=800,scrollbars=yes');
      
      if (!newWindow) {
        alert('Popup blocked! Please allow popups and try again.');
        return;
      }
      
      // Parse CSV content
      const lines = csvContent.split('\n').filter(line => line.trim().length > 0);
      const dataLines = lines.filter(line => !line.startsWith('#'));
      
      if (dataLines.length === 0) {
        newWindow.document.write('<html><body><h1>No data found in CSV file</h1></body></html>');
        return;
      }
      
      const headers = dataLines[0].split(',');
      const rows = dataLines.slice(1);
      
      // Create HTML content
      let html = `<!DOCTYPE html>
<html>
<head>
    <title>CSV Table Viewer - ${filename}</title>
    <meta charset="UTF-8">
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 20px;
            background: #f5f5f5;
        }
        .header {
            background: white;
            padding: 20px;
            border-radius: 8px;
            margin-bottom: 20px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        .header h1 {
            color: #333;
            margin: 0 0 10px 0;
        }
        .nav-buttons {
            margin-bottom: 15px;
        }
        .btn {
            padding: 8px 16px;
            margin-right: 10px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            text-decoration: none;
            display: inline-block;
            font-size: 14px;
        }
        .btn-primary {
            background: #4CAF50;
            color: white;
        }
        .btn-secondary {
            background: #2196F3;
            color: white;
        }
        .btn:hover {
            opacity: 0.8;
        }
        .table-container {
            background: white;
            border-radius: 8px;
            overflow: hidden;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        table {
            width: 100%;
            border-collapse: collapse;
        }
        th, td {
            padding: 12px 8px;
            text-align: left;
            border-bottom: 1px solid #ddd;
            font-size: 14px;
        }
        th {
            background: #4CAF50;
            color: white;
            font-weight: bold;
            position: sticky;
            top: 0;
            z-index: 10;
        }
        tr:nth-child(even) {
            background: #f9f9f9;
        }
        tr:hover {
            background: #f0f8ff;
        }
        .state-compression {
            background: #ffebee !important;
            color: #c62828;
            font-weight: bold;
        }
        .state-recoil {
            background: #e8f5e8 !important;
            color: #2e7d32;
            font-weight: bold;
        }
        .state-quietude {
            background: #f3e5f5 !important;
            color: #7b1fa2;
            font-weight: bold;
        }
        .good-true {
            color: #4CAF50;
            font-weight: bold;
        }
        .good-false {
            color: #f44336;
            font-weight: bold;
        }
        .stats {
            background: #e3f2fd;
            padding: 15px;
            border-radius: 5px;
            margin-top: 20px;
        }
        .comment-row {
            background: #fff3cd !important;
            font-style: italic;
            color: #856404;
            text-align: center;
            font-weight: bold;
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>📊 CSV Data: ${filename}</h1>
        <div class="nav-buttons">
            <a href="/${filename}" class="btn btn-primary" target="_blank">📥 Download Raw CSV</a>
            <button class="btn btn-secondary" onclick="window.location.reload()">🔄 Refresh</button>
            <button class="btn btn-secondary" onclick="window.print()">🖨️ Print</button>
        </div>
        <p><strong>Total Rows:</strong> ${rows.length} | <strong>Columns:</strong> ${headers.length}</p>
    </div>
    
    <div class="table-container">
        <table>
            <thead>
                <tr>`;
      
      // Add headers
      headers.forEach(header => {
        html += `<th>${header.trim()}</th>`;
      });
      
      html += `</tr>
              </thead>
              <tbody>`;
      
      // Add data rows (limit to 1000 rows for performance)
      const maxRows = Math.min(rows.length, 1000);
      
      for (let i = 0; i < maxRows; i++) {
        const row = rows[i];
        if (row.trim().length === 0) continue;
        
        // Handle comment rows
        if (row.startsWith('#')) {
          html += `<tr class="comment-row"><td colspan="${headers.length}">${row.substring(1)}</td></tr>`;
          continue;
        }
        
        const cells = row.split(',');
        html += '<tr>';
        
        cells.forEach((cell, index) => {
          const trimmedCell = cell.trim();
          let cellClass = '';
          
          // Add special styling for certain columns
          if (headers[index] && headers[index].toLowerCase().includes('state')) {
            if (trimmedCell.toLowerCase() === 'compression') cellClass = 'state-compression';
            else if (trimmedCell.toLowerCase() === 'recoil') cellClass = 'state-recoil';
            else if (trimmedCell.toLowerCase() === 'quietude') cellClass = 'state-quietude';
          } else if (headers[index] && headers[index].toLowerCase().includes('good')) {
            if (trimmedCell.toLowerCase() === 'true') cellClass = 'good-true';
            else if (trimmedCell.toLowerCase() === 'false') cellClass = 'good-false';
          }
          
          html += `<td class="${cellClass}">${trimmedCell}</td>`;
        });
        
        html += '</tr>';
      }
      
      html += `</tbody>
          </table>
      </div>`;
      
      if (rows.length > 1000) {
        html += `<div class="stats">
            <strong>⚠️ Note:</strong> Showing first 1000 rows of ${rows.length} total rows. 
            <a href="/${filename}" target="_blank">Download the complete CSV file</a> to see all data.
        </div>`;
      }
      
      html += `
      <div class="stats">
          <strong>💡 Column Guide:</strong><br>
          • <strong>ChipID:</strong> Unique device identifier<br>
          • <strong>SessionID:</strong> Training session number<br>
          • <strong>Timestamp:</strong> Time in milliseconds<br>
          • <strong>State:</strong> <span class="state-compression">compression</span>, <span class="state-recoil">recoil</span>, or <span class="state-quietude">quietude</span><br>
          • <strong>IsGood:</strong> <span class="good-true">true</span> = good quality, <span class="good-false">false</span> = needs improvement<br>
          • <strong>Rate:</strong> CPR rate (compressions/minute)<br>
          • <strong>CCF:</strong> Chest Compression Fraction (%)
      </div>
</body>
</html>`;
      
      newWindow.document.write(html);
      newWindow.document.close();
    }

    function openCSVTable(filename) {
      console.log('openCSVTable() called with filename:', filename);
      
      // Remove leading slash if present and use direct access
      const cleanFilename = filename.startsWith('/') ? filename.substring(1) : filename;
      const directUrl = `/${cleanFilename}`;
      
      console.log('Opening direct file URL:', directUrl);
      window.open(directUrl, '_blank');
    }

    // =============================================
    // UTILITY FUNCTIONS
    // =============================================
    function formatBytes(bytes) {
      if (bytes === 0) return '0 B';
      const k = 1024;
      const sizes = ['B', 'KB', 'MB'];
      const i = Math.floor(Math.log(bytes) / Math.log(k));
      return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
    }

    function getFileIcon(filename) {
      const name = filename.toLowerCase();
      if (name.endsWith('.csv')) return '📊';
      if (name.endsWith('.json')) return '📄';
      if (name.endsWith('.html')) return '🌐';
      if (name.endsWith('.css')) return '🎨';
      if (name.endsWith('.js')) return '⚡';
      if (name.endsWith('.png') || name.endsWith('.jpg')) return '🖼️';
      if (name.endsWith('.mp3')) return '🎵';
      if (name.endsWith('.svg')) return '🎨';
      return '📄';
    }

    function cleanFileName(filename) {
      // Remove leading slash if present for display and API calls
      return filename.startsWith('/') ? filename.substring(1) : filename;
    }
    // =============================================
    // DATA LOADING FUNCTIONS
    // =============================================
    async function loadFileData() {
      try {
        const response = await fetch('/files_api');
        if (!response.ok) {
          throw new Error('Failed to fetch file data');
        }
        const data = await response.json();
        return data;
      } catch (error) {
        console.error('Error loading file data:', error);
        return { files: [], next_session: 1, csv_file_exists: false, csv_file_name: '' };
      }
    }

    function categorizeFiles(files) {
      const categories = {
        csv: [],
        database: [],
        system: [],
        other: []
      };

      files.forEach(file => {
        const name = file.name.toLowerCase();
        if (name.endsWith('.csv')) {
          categories.csv.push(file);
        } else if (name.endsWith('.json') && (name.includes('session') || name.includes('events'))) {
          categories.database.push(file);
        } else if (name.endsWith('.json') || name.endsWith('.html') || name.endsWith('.css') || name.endsWith('.js') || name.endsWith('.png') || name.endsWith('.svg') || name.endsWith('.mp3')) {
          categories.system.push(file);
        } else {
          categories.other.push(file);
        }
      });

      return categories;
    }

    function updateCSVInfo(data) {
      const csvInfo = document.getElementById('csv-info');
      const csvExists = data.csv_file_exists;
      const nextSession = data.next_session || 1;
      const csvFileName = data.csv_file_name || 'Unknown';
      const chipId = data.chip_id || 'Unknown';
      
      let csvFile = null;
      if (csvExists && data.files) {
        // Look for CSV file in files array - handle both with and without leading slash
        csvFile = data.files.find(f => {
          const fileName = f.name.startsWith('/') ? f.name.substring(1) : f.name;
          const csvName = csvFileName.startsWith('/') ? csvFileName.substring(1) : csvFileName;
          return fileName === csvName;
        });
      }
      
      let infoHtml = `
        <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px;">
          <div>
            <strong>CSV File Status:</strong><br>
            ${csvExists ? '✅ Exists' : '❌ Not Found'}
          </div>
          <div>
            <strong>Next Session Number:</strong><br>
            ${nextSession}
          </div>
          <div>
            <strong>Device Chip ID:</strong><br>
            ${chipId}
          </div>
          <div>
            <strong>CSV Filename:</strong><br>
            ${cleanFileName(csvFileName)}
          </div>
      `;
      
      if (csvFile) {
        infoHtml += `
          <div>
            <strong>File Size:</strong><br>
            ${formatBytes(csvFile.size)}
          </div>
        `;
      }
      
      infoHtml += '</div>';
      
      if (!csvExists) {
        infoHtml += `
          <div class="info-message">
            <strong>ℹ️ Info:</strong> CSV file will be created automatically when you start your first training session.
          </div>
        `;
      } else {
        infoHtml += `
          <div class="success-message">
            <strong>✅ CSV File Ready:</strong><br>
            • File accessible at: <code>/${chipId}.csv</code><br>
            • Use buttons above to view or download the data<br>
            • "View as Table" provides formatted view with color coding
          </div>
        `;
      }
      
      csvInfo.innerHTML = infoHtml;
    }

    function updateStatistics(files, data) {
      const totalFiles = files.length;
      const totalSize = files.reduce((sum, file) => sum + file.size, 0);
      const csvFiles = files.filter(f => f.name.toLowerCase().endsWith('.csv')).length;
      const nextSession = data.next_session || 1;

      const statsHtml = `
        <div class="stat-card">
          <div class="stat-value">${totalFiles}</div>
          <div class="stat-label">Total Files</div>
        </div>
        <div class="stat-card">
          <div class="stat-value">${formatBytes(totalSize)}</div>
          <div class="stat-label">Storage Used</div>
        </div>
        <div class="stat-card">
          <div class="stat-value">${csvFiles}</div>
          <div class="stat-label">CSV Files</div>
        </div>
        <div class="stat-card">
          <div class="stat-value">${nextSession}</div>
          <div class="stat-label">Next Session</div>
        </div>
      `;

      document.getElementById('stats-grid').innerHTML = statsHtml;
    }

    function renderFileCategory(files, containerId, emptyMessage) {
      const container = document.getElementById(containerId);
      
      if (files.length === 0) {
        container.innerHTML = `
          <div class="empty-state">
            <h3>No files found</h3>
            <p>${emptyMessage}</p>
          </div>
        `;
        return;
      }

      const filesHtml = files.map(file => {
        const cleanName = cleanFileName(file.name);
        
        return `
          <div class="file-card">
            <div class="file-name">${getFileIcon(file.name)} ${file.name}</div>
            <div class="file-info">
              Size: ${formatBytes(file.size)}
            </div>
            <div class="file-actions">
              <a href="/${cleanName}" class="btn btn-primary btn-small" target="_blank">👀 View Raw</a>
              <a href="/${cleanName}" class="btn btn-secondary btn-small" download>⬇️ Download</a>
              ${file.name.endsWith('.csv') ? 
                `<button class="btn btn-secondary btn-small" onclick="openCSVTable('${cleanName.replace(/'/g, "\\'")}')">📊 Table</button>` : 
                ''}
            </div>
          </div>
        `;
      }).join('');

      container.innerHTML = `<div class="file-grid">${filesHtml}</div>`;
    }

    // =============================================
    // MAIN FUNCTIONS
    // =============================================
    async function refreshData() {
      // Show loading states
      document.getElementById('stats-grid').innerHTML = '<div class="loading">Loading statistics...</div>';
      document.getElementById('csv-files-container').innerHTML = '<div class="loading">Loading CSV files...</div>';
      document.getElementById('database-files-container').innerHTML = '<div class="loading">Loading database files...</div>';
      document.getElementById('system-files-container').innerHTML = '<div class="loading">Loading system files...</div>';
      document.getElementById('csv-info').innerHTML = '<div class="loading">Loading CSV information...</div>';

      try {
        console.log('Fetching file data from /files_api...');
        const data = await loadFileData();
        console.log('Received data:', data);
        
        const files = data.files || [];
        const categories = categorizeFiles(files);

        // Update CSV info
        updateCSVInfo(data);

        // Update statistics
        updateStatistics(files, data);

        // Render file categories
        renderFileCategory(categories.csv, 'csv-files-container', 'No CPR training data files found. Start a training session to generate CSV data.');
        renderFileCategory(categories.database, 'database-files-container', 'No database files found. Database files are created automatically during training.');
        renderFileCategory([...categories.system, ...categories.other], 'system-files-container', 'No system files found.');

        console.log('Data refreshed successfully');
        console.log('CSV files found:', categories.csv);
        
      } catch (error) {
        console.error('Error refreshing data:', error);
        
        // Show error states
        const errorMessage = '<div class="empty-state"><h3>Error Loading Files</h3><p>Please check your connection and try again.</p></div>';
        document.getElementById('csv-files-container').innerHTML = errorMessage;
        document.getElementById('database-files-container').innerHTML = errorMessage;
        document.getElementById('system-files-container').innerHTML = errorMessage;
        document.getElementById('stats-grid').innerHTML = '<div class="stat-card"><div class="stat-value">Error</div><div class="stat-label">Loading Failed</div></div>';
        document.getElementById('csv-info').innerHTML = '<div class="error-message">Error loading CSV information. Check console for details.</div>';
      }
    }

    // =============================================
    // DEBUG FUNCTIONS
    // =============================================
    function debugCSV() {
      console.log('=== CSV DEBUG START ===');
      
      Promise.all([
        fetch('/status').then(r => r.json()),
        fetch('/files_api').then(r => r.json())
      ]).then(([statusData, filesData]) => {
        console.log('Status data:', statusData);
        console.log('Files data:', filesData);
        
        console.log('Chip ID from status:', statusData.chip_id);
        console.log('CSV filename from status:', statusData.csv_file_name);
        console.log('CSV exists from status:', statusData.csv_file_exists);
        
        const csvFiles = filesData.files.filter(f => f.name.toLowerCase().endsWith('.csv'));
        console.log('CSV files found:', csvFiles);
        
        // Test direct access
        if (statusData.chip_id) {
          const testUrl = `/${statusData.chip_id}.csv`;
          console.log('Testing direct access:', testUrl);
          window.open(testUrl, '_blank');
        }
        
      }).catch(error => {
        console.error('Debug failed:', error);
      });
      
      console.log('=== CSV DEBUG END ===');
    }

    // =============================================
    // INITIALIZATION
    // =============================================
    document.addEventListener('DOMContentLoaded', function() {
      console.log('Data management page loaded with direct file access');
      
      // Add debug button to nav
      setTimeout(() => {
        const navButtons = document.querySelector('.nav-buttons');
        if (navButtons) {
          const debugBtn = document.createElement('button');
          debugBtn.className = 'btn btn-secondary';
          debugBtn.textContent = '🔍 Debug';
          debugBtn.onclick = debugCSV;
          debugBtn.style.fontSize = '0.8rem';
          navButtons.appendChild(debugBtn);
        }
      }, 1000);
      
      refreshData();
    });

    // Auto-refresh every 30 seconds
    setInterval(refreshData, 30000);

    // =============================================
    // GLOBAL DEBUG ACCESS
    // =============================================
    window.debugCSV = debugCSV;
    window.viewCSVData = viewCSVData;
    window.viewCSVAsTable = viewCSVAsTable;

    console.log('Data management loaded. Available debug functions:');
    console.log('- debugCSV() - Test CSV access');
    console.log('- viewCSVData() - Open raw CSV');
    console.log('- viewCSVAsTable() - Open formatted table');