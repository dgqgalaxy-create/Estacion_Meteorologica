#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Estacion Meteorologica Pro</title>
    
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600;700&display=swap" rel="stylesheet">
    <link href='https://unpkg.com/boxicons@2.1.4/css/boxicons.min.css' rel='stylesheet'>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>

    <style>
        :root {
            --bg-color: #f0f4f8;
            --bg-gradient: linear-gradient(135deg, #e0c3fc 0%, #8ec5fc 100%);
            --card-bg: rgba(255, 255, 255, 0.75);
            --card-border: rgba(255, 255, 255, 0.4);
            --text-main: #2d3748;
            --text-muted: #718096;
            --primary: #4299e1;
            --success: #48bb78;
            --danger: #f56565;
            --warning: #ed8936;
            --shadow: 0 8px 32px 0 rgba(31, 38, 135, 0.15);
        }

        @media (prefers-color-scheme: dark) {
            :root {
                --bg-color: #1a202c;
                --bg-gradient: linear-gradient(135deg, #2d3748 0%, #1a202c 100%);
                --card-bg: rgba(45, 55, 72, 0.85);
                --card-border: rgba(255, 255, 255, 0.05);
                --text-main: #f7fafc;
                --text-muted: #a0aec0;
                --shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.5);
            }
        }

        body { 
            font-family: 'Inter', sans-serif; 
            margin: 0; 
            padding: 20px; 
            background: var(--bg-color);
            background-image: var(--bg-gradient);
            background-attachment: fixed;
            color: var(--text-main);
            min-height: 100vh;
            display: flex;
            justify-content: center;
        }

        .container { 
            width: 100%;
            max-width: 900px;
        }

        .header {
            text-align: center;
            margin-bottom: 30px;
            animation: fadeInDown 0.8s ease;
        }

        .header h1 {
            font-size: 2.5rem;
            margin: 0;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 10px;
        }

        .header p {
            color: var(--text-muted);
            margin-top: 5px;
            font-size: 0.95rem;
        }

        .glass-panel {
            background: var(--card-bg);
            backdrop-filter: blur(12px);
            -webkit-backdrop-filter: blur(12px);
            border: 1px solid var(--card-border);
            border-radius: 20px;
            padding: 25px;
            box-shadow: var(--shadow);
            margin-bottom: 25px;
            transition: transform 0.3s ease, box-shadow 0.3s ease;
            animation: fadeInUp 0.8s ease;
        }

        .glass-panel:hover {
            transform: translateY(-2px);
            box-shadow: 0 12px 40px 0 rgba(31, 38, 135, 0.2);
        }

        .panel-title {
            font-size: 1.2rem;
            font-weight: 600;
            margin-top: 0;
            margin-bottom: 20px;
            display: flex;
            align-items: center;
            gap: 8px;
            color: var(--primary);
        }

        .metrics-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(130px, 1fr));
            gap: 15px;
        }

        .metric-card {
            background: rgba(255, 255, 255, 0.05);
            border-radius: 15px;
            padding: 15px;
            text-align: center;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            border: 1px solid var(--card-border);
            transition: all 0.3s ease;
        }

        .metric-card:hover {
            background: rgba(255, 255, 255, 0.1);
            transform: scale(1.02);
        }

        .metric-icon {
            font-size: 2rem;
            margin-bottom: 10px;
        }

        .icon-temp { color: #f56565; }
        .icon-hum { color: #4299e1; }
        .icon-pres { color: #48bb78; }
        .icon-hi { color: #ed8936; }
        .icon-dp { color: #9f7aea; }
        .icon-alt { color: #a0aec0; }

        .metric-value {
            font-size: 1.8rem;
            font-weight: 700;
            margin: 0;
            line-height: 1.2;
        }

        .metric-label {
            font-size: 0.8rem;
            color: var(--text-muted);
            text-transform: uppercase;
            letter-spacing: 0.5px;
            margin-top: 5px;
        }

        .record-values {
            font-size: 0.9rem;
            margin-top: 8px;
            display: flex;
            gap: 10px;
        }

        .record-max { color: #f56565; font-weight: 600;}
        .record-min { color: #4299e1; font-weight: 600;}

        .chart-container {
            position: relative;
            height: 350px;
            width: 100%;
        }

        .controls {
            display: flex;
            flex-wrap: wrap;
            gap: 15px;
            justify-content: center;
            align-items: center;
            margin-top: 10px;
        }

        .btn {
            background: var(--primary);
            color: white;
            border: none;
            padding: 10px 20px;
            border-radius: 30px;
            font-size: 0.9rem;
            font-weight: 600;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 8px;
            transition: all 0.3s ease;
            text-decoration: none;
        }

        .btn:hover {
            opacity: 0.9;
            transform: translateY(-2px);
            box-shadow: 0 4px 12px rgba(0,0,0,0.15);
        }

        .btn-danger { background: var(--danger); }
        .btn-success { background: var(--success); }

        .status-bar {
            display: flex;
            justify-content: space-between;
            align-items: center;
            font-size: 0.85rem;
            color: var(--text-muted);
            margin-top: 15px;
            padding-top: 15px;
            border-top: 1px solid var(--card-border);
        }

        .badge {
            padding: 4px 10px;
            border-radius: 12px;
            font-size: 0.75rem;
            font-weight: bold;
            color: white;
        }

        .status-indicator {
            display: flex;
            align-items: center;
            gap: 8px;
            margin-bottom: 10px;
        }
        .status-dot {
            width: 12px;
            height: 12px;
            border-radius: 50%;
        }
        .dot-ok { background: var(--success); }
        .dot-error { background: var(--danger); }
        .dot-warning { background: var(--warning); }

        @keyframes fadeInDown {
            from { opacity: 0; transform: translateY(-20px); }
            to { opacity: 1; transform: translateY(0); }
        }

        @keyframes fadeInUp {
            from { opacity: 0; transform: translateY(20px); }
            to { opacity: 1; transform: translateY(0); }
        }

    </style>
</head>
<body>

    <div class="container">
        
        <div class="header">
            <h1><i class='bx bx-cloud-light-rain'></i> Estacion Clima Pro</h1>
            <p>Sincronizado ESP32: <span id="horaWeb">%TIEMPO%</span></p>
        </div>

        <!-- Condiciones Actuales -->
        <div class="glass-panel">
            <h2 class="panel-title"><i class='bx bx-podcast'></i> Condiciones Actuales</h2>
            
            <div class="metrics-grid">
                
                <div class="metric-card">
                    <i class='bx bxs-thermometer metric-icon icon-temp'></i>
                    <div class="metric-value" id="tempVal">%TEMPERATURA%<small style="font-size:1rem">°C</small></div>
                    <div class="metric-label">Temperatura</div>
                </div>

                <div class="metric-card">
                    <i class='bx bx-water metric-icon icon-hum'></i>
                    <div class="metric-value" id="humVal">%HUMEDAD%<small style="font-size:1rem">%</small></div>
                    <div class="metric-label">Humedad</div>
                </div>

                <div class="metric-card">
                    <i class='bx bx-tachometer metric-icon icon-pres'></i>
                    <div class="metric-value" style="font-size: 1.4rem"><span id="presVal">%PRESION%</span><br><small style="font-size:0.8rem">hPa</small></div>
                    <div class="metric-label">Presion</div>
                </div>

                <div class="metric-card">
                    <i class='bx bxs-hot metric-icon icon-hi'></i>
                    <div class="metric-value" id="hiVal">%SENSACION%<small style="font-size:1rem">°C</small></div>
                    <div class="metric-label">Sensacion Termica</div>
                </div>

                <div class="metric-card">
                    <i class='bx bxs-droplet metric-icon icon-dp'></i>
                    <div class="metric-value" id="dpVal">%ROCIO%<small style="font-size:1rem">°C</small></div>
                    <div class="metric-label">Pto. de Rocio</div>
                </div>

                <div class="metric-card">
                    <i class='bx bx-target-lock metric-icon icon-alt'></i>
                    <div class="metric-value" id="altVal">%ALTURA%<small style="font-size:1rem">m</small></div>
                    <div class="metric-label">Altitud Aprox.</div>
                </div>
                
            </div>
        </div>

        <!-- Records Diarios -->
        <div class="glass-panel" style="border-left: 4px solid var(--warning);">
            <h2 class="panel-title" style="color: var(--warning)"><i class='bx bx-trophy'></i> Records de Hoy</h2>
            <div class="metrics-grid">
                <div class="metric-card">
                    <div class="metric-label">Temperatura</div>
                    <div class="record-values">
                        <span class="record-max"><i class='bx bx-up-arrow-alt'></i> <span id="tmaxVal">%TEMP_MAX%</span>°</span>
                        <span class="record-min"><i class='bx bx-down-arrow-alt'></i> <span id="tminVal">%TEMP_MIN%</span>°</span>
                    </div>
                </div>
                <div class="metric-card">
                    <div class="metric-label">Humedad</div>
                    <div class="record-values">
                        <span class="record-max"><i class='bx bx-up-arrow-alt'></i> <span id="hmaxVal">%HUM_MAX%</span>%</span>
                        <span class="record-min"><i class='bx bx-down-arrow-alt'></i> <span id="hminVal">%HUM_MIN%</span>%</span>
                    </div>
                </div>
                <div class="metric-card">
                    <div class="metric-label">Presion</div>
                    <div class="record-values">
                        <span class="record-max"><i class='bx bx-up-arrow-alt'></i> <span id="pmaxVal">%PRES_MAX%</span></span>
                        <span class="record-min"><i class='bx bx-down-arrow-alt'></i> <span id="pminVal">%PRES_MIN%</span></span>
                    </div>
                </div>
            </div>
        </div>

        <!-- Estado del Sistema -->
        <div class="glass-panel">
            <h2 class="panel-title"><i class='bx bx-info-circle'></i> Estado del Sistema</h2>
            <div class="status-indicator">
                <span class="status-dot dot-ok" id="dotWifi"></span>
                <span>WiFi: <strong id="wifiStatus">%ESTADO_WIFI%</strong></span>
            </div>
            <div class="status-indicator">
                <span class="status-dot dot-ok" id="dotSensor"></span>
                <span>Sensor: <strong id="sensorStatus">%ESTADO_SENSOR%</strong></span>
            </div>
            <div class="status-indicator">
                <span class="status-dot dot-ok" id="dotSheets"></span>
                <span>Google Sheets: <strong id="sheetsStatus">%ESTADO_SHEETS%</strong></span>
            </div>
            <div class="status-indicator">
                <span class="status-dot dot-ok" id="dotFirmware"></span>
                <span>OTA: <strong id="firmwareStatus">%ESTADO_FIRMWARE%</strong></span>
            </div>
            <div class="status-indicator">
                <span class="status-dot dot-ok"></span>
                <span>Firmware: <strong id="firmwareVersion">%FIRMWARE_VERSION%</strong></span>
            </div>
            <div class="status-indicator">
                <span class="status-dot dot-ok"></span>
                <span>Ultima actualizacion: <strong id="firmwareDate">%FIRMWARE_DATE%</strong></span>
            </div>
        </div>

        <!-- Grafico de Historial Continuo -->
        <div class="glass-panel">
            <h2 class="panel-title"><i class='bx bx-line-chart'></i> Historial (Ultimos 4 Dias)</h2>
            <div class="chart-container">
                <canvas id="mainChart"></canvas>
            </div>
        </div>

        <!-- Controles y Sistema -->
        <div class="glass-panel">
            <h2 class="panel-title"><i class='bx bx-cog'></i> Configuracion y Sistema</h2>
            
            <div class="controls">
                <a href="/toggle" class="btn %TOGGLE_CLASS%">%TOGGLE_TEXT%</a>
                <button class="btn btn-primary" onclick="window.location.reload()"><i class='bx bx-refresh'></i> Refrescar UI</button>
                <a href="/retry" class="btn" style="background: var(--warning);"><i class='bx bx-refresh'></i> Reintentar Envio</a>
                <a href="/checkupdate" class="btn btn-primary"><i class='bx bx-cloud-download'></i> Buscar actualizacion</a>
                <form action="/setinterval" method="GET" style="display: flex; align-items: center; gap: 10px; background: rgba(0,0,0,0.05); padding: 5px 15px; border-radius: 30px; margin:0;">
                    <label style="font-size:0.9rem; font-weight:600;">Intervalo (s):</label>
                    <input type="number" name="segundos" value="%INTERVALO_SEC%" min="5" style="width: 60px; padding: 5px; border-radius: 5px; border:1px solid var(--card-border); background: transparent; color: var(--text-main);">
                    <button type="submit" class="btn btn-success" style="padding: 6px 12px; font-size: 0.8rem;"><i class='bx bx-save'></i></button>
                </form>
                <a href="/resetwifi" class="btn btn-danger" onclick="return confirm('¿Borrar credenciales WiFi?')"><i class='bx bx-wifi-off'></i> Reset WiFi</a>
            </div>

            <div class="status-bar">
                <span><i class='bx bx-wifi'></i> IP: <span id="ipWeb">%IP%</span></span>
                <span><i class='bx bx-signal-4'></i> Senal: <span id="rssiWeb">%RSSI%</span> dBm</span>
                <span class="badge" style="background:var(--success)" id="estadoBadge">%ESTADO%</span>
            </div>
        </div>

    </div>

    <script>
        const ctx = document.getElementById('mainChart').getContext('2d');
        
        const labels = Array.from({length: 96}, (_, i) => `-${95-i}h`);

        const isDarkMode = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
        const gridColor = isDarkMode ? 'rgba(255, 255, 255, 0.1)' : 'rgba(0, 0, 0, 0.1)';
        const textColor = isDarkMode ? '#a0aec0' : '#718096';

        const myChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: labels,
                datasets: [
                    {
                        label: 'Temperatura (°C)',
                        data: [],
                        borderColor: '#f56565',
                        backgroundColor: 'rgba(245, 101, 101, 0.1)',
                        borderWidth: 2,
                        tension: 0.4,
                        fill: true,
                        yAxisID: 'y',
                        pointRadius: 1, 
                        pointHitRadius: 10
                    },
                    {
                        label: 'Humedad (%)',
                        data: [],
                        borderColor: '#4299e1',
                        backgroundColor: 'rgba(66, 153, 225, 0.1)',
                        borderWidth: 2,
                        tension: 0.4,
                        fill: true,
                        yAxisID: 'y1',
                        pointRadius: 1,
                        pointHitRadius: 10
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                interaction: {
                    mode: 'index',
                    intersect: false,
                },
                plugins: {
                    legend: {
                        labels: { color: textColor, font: { family: 'Inter', weight: 600 } }
                    },
                    tooltip: {
                        backgroundColor: isDarkMode ? 'rgba(0,0,0,0.8)' : 'rgba(255,255,255,0.9)',
                        titleColor: isDarkMode ? '#fff' : '#000',
                        bodyColor: isDarkMode ? '#fff' : '#000',
                        borderColor: isDarkMode ? '#4a5568' : '#e2e8f0',
                        borderWidth: 1,
                        padding: 10
                    }
                },
                scales: {
                    x: {
                        grid: { color: gridColor },
                        ticks: { color: textColor, maxTicksLimit: 12 }
                    },
                    y: {
                        type: 'linear',
                        display: true,
                        position: 'left',
                        grid: { color: gridColor },
                        ticks: { color: textColor },
                        title: { display: true, text: 'Temperatura (°C)', color: '#f56565' }
                    },
                    y1: {
                        type: 'linear',
                        display: true,
                        position: 'right',
                        grid: { drawOnChartArea: false },
                        ticks: { color: textColor },
                        title: { display: true, text: 'Humedad (%)', color: '#4299e1' }
                    }
                }
            }
        });

        function fetchData() {
            fetch('/data.json').then(r => r.json()).then(d => {
                const tempCont = [...d.t3.slice(0,24), ...d.t2.slice(0,24), ...d.t1.slice(0,24), ...d.t0.slice(0,24)];
                const humCont = [...d.h3.slice(0,24), ...d.h2.slice(0,24), ...d.h1.slice(0,24), ...d.h0.slice(0,24)];
                
                myChart.data.datasets[0].data = tempCont;
                myChart.data.datasets[1].data = humCont;
                myChart.update();
            }).catch(e => console.error(e));
        }

        function fetchCurrent() {
            fetch('/api/current').then(r => r.json()).then(d => {
                document.getElementById('tempVal').innerHTML = d.temp + '<small style="font-size:1rem">°C</small>';
                document.getElementById('humVal').innerHTML = d.hum + '<small style="font-size:1rem">%</small>';
                document.getElementById('presVal').textContent = d.pres;
                document.getElementById('hiVal').innerHTML = d.hi + '<small style="font-size:1rem">°C</small>';
                document.getElementById('dpVal').innerHTML = d.dp + '<small style="font-size:1rem">°C</small>';
                document.getElementById('altVal').innerHTML = d.alt + '<small style="font-size:1rem">m</small>';
                
                document.getElementById('tmaxVal').textContent = d.tmax < -50 ? '--' : d.tmax;
                document.getElementById('tminVal').textContent = d.tmin > 50 ? '--' : d.tmin;
                document.getElementById('hmaxVal').textContent = d.hmax < 1 ? '--' : d.hmax;
                document.getElementById('hminVal').textContent = d.hmin > 99 ? '--' : d.hmin;
                document.getElementById('pmaxVal').textContent = d.pmax < 1 ? '--' : d.pmax;
                document.getElementById('pminVal').textContent = d.pmin > 1999 ? '--' : d.pmin;
                
                document.getElementById('wifiStatus').textContent = d.wifi;
                document.getElementById('sensorStatus').textContent = d.sensor;
                document.getElementById('sheetsStatus').textContent = d.sheets;
                document.getElementById('firmwareStatus').textContent = d.firmware;
                document.getElementById('horaWeb').textContent = d.hora;
                document.getElementById('ipWeb').textContent = d.ip;
                document.getElementById('rssiWeb').textContent = d.rssi;
                document.getElementById('estadoBadge').textContent = d.estado;
                document.getElementById('firmwareVersion').textContent = d.version;
                document.getElementById('firmwareDate').textContent = d.actualizado;
                
                const dotWifi = document.getElementById('dotWifi');
                const dotSensor = document.getElementById('dotSensor');
                const dotSheets = document.getElementById('dotSheets');
                const dotFirmware = document.getElementById('dotFirmware');
                
                dotWifi.className = d.wifi === 'Conectado' ? 'status-dot dot-ok' : 'status-dot dot-error';
                dotSensor.className = d.sensor === 'OK' ? 'status-dot dot-ok' : 'status-dot dot-error';
                dotSheets.className = d.sheets === 'OK' ? 'status-dot dot-ok' : (d.sheets === 'Pausado' ? 'status-dot dot-warning' : 'status-dot dot-error');
                const fwOk = (d.firmware === 'Actualizado' || d.firmware === 'Firmware comprobado');
                dotFirmware.className = fwOk ? 'status-dot dot-ok' : (d.firmware === 'Actualizando...' ? 'status-dot dot-warning' : 'status-dot dot-error');
            }).catch(e => console.error(e));
        }

        fetchData();
        fetchCurrent();
        setInterval(fetchData, 15000);
        setInterval(fetchCurrent, 10000);
    </script>
</body>
</html>
)rawliteral";

#endif