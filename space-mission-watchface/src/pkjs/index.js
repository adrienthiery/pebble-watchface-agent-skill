// PebbleKit JS — Space Mission Watchface
// Stations: wheretheiss.at (ISS NORAD 25544), N2YO (CSS NORAD 48274)
// Missions: Launch Library 2 — cached in localStorage for 1 hour

var CACHE_KEY = 'spc_v8'; // bumped: 14-char mission names
var CACHE_TTL = 3600000; // 1 hour in ms
var N2YO_KEY_STORAGE = 'n2yo_key';
var DEFAULT_N2YO_KEY = 'V5GWV8-RVBHRJ-PGWZDB-5PKU';

var s_userLat = 0;
var s_userLon = 0;
var s_pendingRequests = 0;
var s_issResult = null;
var s_cssResult = null;
var s_missionsResult = null;

function getN2YOKey() {
    try {
        var key = localStorage.getItem(N2YO_KEY_STORAGE);
        return (key && key.length > 0) ? key : DEFAULT_N2YO_KEY;
    } catch(e) { return DEFAULT_N2YO_KEY; }
}

// ---- XHR helper ----

var xhrRequest = function(url, type, callback) {
    var xhr = new XMLHttpRequest();
    xhr.onload = function() { callback(this.responseText); };
    xhr.onerror = function() {
        console.log('XHR error: ' + url);
        callback(null);
    };
    xhr.open(type, url);
    xhr.send();
};

// ---- Mission cache (localStorage) ----

function loadMissionCache() {
    try {
        var raw = localStorage.getItem(CACHE_KEY);
        if (!raw) return null;
        return JSON.parse(raw);
    } catch(e) { return null; }
}

function saveMissionCache(missions) {
    try {
        localStorage.setItem(CACHE_KEY, JSON.stringify({
            ts: Date.now(),
            missions: missions
        }));
    } catch(e) {
        console.log('Cache save error: ' + e);
    }
}

function isMissionCacheValid() {
    var c = loadMissionCache();
    return c !== null &&
           (Date.now() - c.ts) < CACHE_TTL &&
           c.missions && c.missions.length > 0;  // don't serve empty cache
}

// ---- Data mapping ----

function agencyToCountry(abbrev) {
    if (!abbrev) return 0;
    var a = abbrev.toUpperCase();
    if (a === 'CNSA') return 1;
    if (a === 'ESA' || a === 'CNES' || a === 'DLR' || a === 'ASI') return 2;
    if (a === 'ROSCOSMOS' || a === 'RSC ENERGIA' || a === 'RKA' || a === 'ROSKOSMOS') return 3;
    return 0;
}

// Returns orbit code:
// 0=ORBIT_EARTH, 1=ORBIT_MOON, 2=TRANSIT_TO_MOON, 3=TRANSIT_TO_EARTH, 4=ORBIT_DOCKED
function isLunarMission(destination, orbitName) {
    var d = (destination || '').toLowerCase();
    var o = (orbitName || '').toLowerCase();
    return d.indexOf('lunar') >= 0 || d.indexOf('moon') >= 0 ||
           o.indexOf('lunar') >= 0;
}

// Returns orbit code:
// 0=ORBIT_EARTH, 1=ORBIT_MOON, 2=TRANSIT_TO_MOON, 3=TRANSIT_TO_EARTH, 4=ORBIT_DOCKED
function destinationToOrbit(destination, orbitName, launchDateStr) {
    var d = (destination || '').toLowerCase();
    // Docked to CSS/Tiangong (check before generic "space station")
    if (d.indexOf('tiangong') >= 0 || d.indexOf('chinese space station') >= 0) {
        return 6;  // ORBIT_DOCKED_CSS
    }
    // Docked to ISS
    if (d === 'iss' ||
        d.indexOf('international space station') >= 0 ||
        d.indexOf('space station') >= 0) {
        return 4;  // ORBIT_DOCKED
    }
    // Moon or lunar missions
    if (isLunarMission(destination, orbitName)) {
        if (launchDateStr) {
            var daysSinceLaunch = (Date.now() - new Date(launchDateStr).getTime()) / 86400000;
            if (daysSinceLaunch < 4) return 2;
        }
        return 1;
    }
    // Heliocentric / deep space
    if (d.indexOf('heliocentric') >= 0 || d.indexOf('deep space') >= 0 ||
        d.indexOf('solar orbit') >= 0) {
        return 5;
    }
    return 0;
}

// Returns the longitude of the station a docked spacecraft is attached to
function dockedStationLon(destination, issLon, cssLon) {
    if (!destination) return issLon;
    var d = destination.toLowerCase();
    if (d.indexOf('tiangong') >= 0 || d.indexOf('chinese space station') >= 0) return cssLon;
    return issLon;
}

// ---- Send to watch ----

// Build the mission portion of a message (country, orbit, name for each slot)
function buildMissionMsg(msg, missions, issLon, cssLon) {
    for (var i = 0; i < Math.min(missions.length, 10); i++) {
        var m = missions[i];
        msg['MISSION_' + i + '_COUNTRY'] = m.country;
        msg['MISSION_' + i + '_ORBIT']   = m.orbit;
        msg['MISSION_' + i + '_NAME']    = m.name || '';
        if (m.orbit === 4) {
            msg['MISSION_' + i + '_LON'] = dockedStationLon(m.destination, issLon, cssLon);
        }
    }
}

// Send cached missions immediately so the watch isn't blank while stations load
function sendCachedMissionsEarly() {
    var missions = s_missionsResult;
    var msg = {
        'USER_LON':    Math.round(s_userLon),
        'ISS_VISIBLE': 0,
        'ISS_LON':     0,
        'CSS_VISIBLE': 0,
        'CSS_LON':     0
    };
    buildMissionMsg(msg, missions, 0, 0);
    console.log('Sending cached missions early (' + missions.length + ')');
    Pebble.sendAppMessage(msg,
        function() { console.log('Early missions sent OK'); },
        function() { console.log('Failed to send early missions'); }
    );
}

function sendDataWhenReady() {
    if (s_pendingRequests > 0) return;
    if (!s_issResult || !s_cssResult || !s_missionsResult) return;

    var msg = {
        'USER_LON':    Math.round(s_userLon),
        'ISS_VISIBLE': s_issResult.visible,
        'ISS_LON':     s_issResult.lon,
        'CSS_VISIBLE': s_cssResult.visible,
        'CSS_LON':     s_cssResult.lon
    };
    buildMissionMsg(msg, s_missionsResult, s_issResult.lon, s_cssResult.lon);

    console.log('Sending: ISS=' + s_issResult.visible + '@' + s_issResult.lon +
                ' CSS=' + s_cssResult.visible + '@' + s_cssResult.lon +
                ' missions=' + s_missionsResult.length);

    Pebble.sendAppMessage(
        msg,
        function() { console.log('Data sent OK'); },
        function() { console.log('Failed to send data'); }
    );
}

// ---- Fetch: stations (always fresh) ----

function fetchStation(noradId, callback) {
    var url = 'https://api.wheretheiss.at/v1/satellites/' + noradId;
    xhrRequest(url, 'GET', function(responseText) {
        if (responseText) {
            try {
                var j = JSON.parse(responseText);
                var lon = parseFloat(j.longitude);
                if (isNaN(lon)) {
                    console.log('Station ' + noradId + ': longitude missing in response');
                    callback({ visible: 0, lon: 0 });
                    return;
                }
                callback({
                    visible: (j.visibility === 'visible') ? 1 : 0,
                    lon: Math.round(lon)
                });
                return;
            } catch(e) {
                console.log('Station parse error (' + noradId + '): ' + e);
            }
        }
        console.log('Station ' + noradId + ': no response');
        callback({ visible: 0, lon: 0 });
    });
}

function fetchISS() {
    s_pendingRequests++;
    fetchStation(25544, function(result) {
        s_pendingRequests--;
        s_issResult = result;
        console.log('ISS: lon=' + result.lon + ' visible=' + result.visible);
        sendDataWhenReady();
    });
}

function fetchCSS() {
    s_pendingRequests++;
    var key = getN2YOKey();
    var url = 'https://api.n2yo.com/rest/v1/satellite/positions/48274/' +
              s_userLat + '/' + s_userLon + '/0/1/&apiKey=' + key;
    xhrRequest(url, 'GET', function(responseText) {
        s_pendingRequests--;
        if (responseText) {
            try {
                var j = JSON.parse(responseText);
                var pos = j.positions && j.positions[0];
                if (pos) {
                    var lon = parseFloat(pos.satlongitude);
                    if (!isNaN(lon)) {
                        var visible = (pos.elevation > 0 && !pos.eclipsed) ? 1 : 0;
                        s_cssResult = { visible: visible, lon: Math.round(lon) };
                        console.log('CSS: lon=' + s_cssResult.lon + ' visible=' + visible);
                        sendDataWhenReady();
                        return;
                    }
                }
                console.log('CSS: unexpected N2YO response');
            } catch(e) {
                console.log('CSS parse error: ' + e);
            }
        } else {
            console.log('CSS: N2YO request failed');
        }
        s_cssResult = { visible: 0, lon: 0 };
        sendDataWhenReady();
    });
}

// ---- Fetch: missions (cached 1 hour via LL2) ----

function fetchMissions() {
    // Serve from cache if still valid — send immediately, don't wait for stations
    if (isMissionCacheValid()) {
        var c = loadMissionCache();
        s_missionsResult = c.missions;
        console.log('Missions from cache (' + s_missionsResult.length + ')');
        sendCachedMissionsEarly();
        // sendDataWhenReady() will fire again once ISS/CSS fetches complete
        return;
    }

    s_pendingRequests++;
    var url = 'https://ll.thespacedevs.com/2.3.0/spacecraft_flights/' +
              '?limit=20&ordering=-mission_end&format=json';
    xhrRequest(url, 'GET', function(responseText) {
        s_pendingRequests--;
        if (responseText) {
            try {
                var json = JSON.parse(responseText);
                var results = json.results || [];
                var inSpaceResults = results.filter(function(f) { return f.spacecraft && f.spacecraft.in_space; });
                console.log('LL2: ' + results.length + ' total, ' + inSpaceResults.length + ' in_space=true');
                var missions = [];

                for (var i = 0; i < results.length && missions.length < 10; i++) {
                    var flight = results[i];

                    // Only include spacecraft currently in space
                    if (!flight.spacecraft || !flight.spacecraft.in_space) continue;

                    var agency = null;
                    // 1. spacecraft_config.agency
                    if (!agency && flight.spacecraft && flight.spacecraft.spacecraft_config &&
                        flight.spacecraft.spacecraft_config.agency) {
                        agency = flight.spacecraft.spacecraft_config.agency.abbrev;
                    }
                    // 2. launch.launch_service_provider
                    if (!agency && flight.launch && flight.launch.launch_service_provider) {
                        agency = flight.launch.launch_service_provider.abbrev;
                    }
                    // 3. mission.agencies[0]
                    if (!agency && flight.mission && flight.mission.agencies &&
                        flight.mission.agencies.length > 0) {
                        agency = flight.mission.agencies[0].abbrev;
                    }
                    // 4. crew fallback
                    if (!agency && flight.crew && flight.crew.length > 0 &&
                        flight.crew[0].astronaut && flight.crew[0].astronaut.agency) {
                        agency = flight.crew[0].astronaut.agency.abbrev;
                    }

                    var destination = flight.destination || '';
                    var orbitName   = (flight.mission && flight.mission.orbit) ? flight.mission.orbit.name : '';
                    var launchDate  = flight.launch ? flight.launch.net : null;
                    var orbit       = destinationToOrbit(destination, orbitName, launchDate);
                    var country     = agencyToCountry(agency);

                    var name = '';
                    if (flight.spacecraft && flight.spacecraft.spacecraft_config &&
                        flight.spacecraft.spacecraft_config.name) {
                        name = flight.spacecraft.spacecraft_config.name.substring(0, 14).toUpperCase();
                    }

                    console.log('Mission: "' + name + '" dest="' + destination +
                                '" orbitName="' + orbitName + '" orbit=' + orbit);
                    missions.push({ country: country, orbit: orbit, name: name, destination: destination });
                }

                s_missionsResult = missions;
                saveMissionCache(missions);
            } catch(e) {
                console.log('LL2 parse error: ' + e);
                s_missionsResult = [];
            }
        } else {
            s_missionsResult = [];
        }
        sendDataWhenReady();
    });
}

// ---- Entry points ----

function fetchAllData() {
    s_issResult = null;
    s_cssResult = null;
    s_missionsResult = null;
    fetchISS();
    fetchCSS();
    fetchMissions();
}

function getLocationAndFetch() {
    navigator.geolocation.getCurrentPosition(
        function(pos) {
            s_userLat = pos.coords.latitude;
            s_userLon = pos.coords.longitude;
            console.log('Location: ' + s_userLat.toFixed(4) + ',' + s_userLon.toFixed(4));
            fetchAllData();
        },
        function(err) {
            console.log('Geolocation error: ' + err.message);
            fetchAllData();
        },
        { timeout: 15000, maximumAge: 300000 }
    );
}

Pebble.addEventListener('ready', function() {
    console.log('Space Mission Watchface JS ready');
    getLocationAndFetch();
});

Pebble.addEventListener('appmessage', function(e) {
    if (e.payload['REQUEST_UPDATE']) {
        console.log('Watch requested update');
        getLocationAndFetch();
    }
});

Pebble.addEventListener('showConfiguration', function() {
    var currentKey = getN2YOKey();
    var html = '<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">' +
        '<style>body{font-family:sans-serif;background:#1a1a2e;color:#eee;padding:20px;margin:0}' +
        'h2{color:#e94560;margin-top:0}label{display:block;margin-bottom:6px;font-size:14px}' +
        'input{width:100%;box-sizing:border-box;padding:10px;background:#16213e;color:#eee;' +
        'border:1px solid #e94560;border-radius:4px;font-size:14px;margin-bottom:16px}' +
        'button{width:100%;padding:12px;background:#e94560;color:#fff;border:none;' +
        'border-radius:4px;font-size:16px;cursor:pointer}' +
        'p{font-size:12px;color:#aaa;margin-top:12px}</style></head>' +
        '<body><h2>Space Mission Settings</h2>' +
        '<label for="key">N2YO API Key (for CSS real-time position)</label>' +
        '<input type="text" id="key" placeholder="' + DEFAULT_N2YO_KEY + '" value="' + currentKey + '">' +
        '<button onclick="save()">Save</button>' +
        '<p>Get a free key at <strong>n2yo.com</strong>. The default key is shared and may be rate-limited.</p>' +
        '<script>function save(){var k=document.getElementById("key").value.trim();' +
        'var r=k.length>0?k:"";' +
        'location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify({n2yo_key:r}));' +
        '}<\/script></body></html>';
    Pebble.openURL('data:text/html,' + encodeURIComponent(html));
});

Pebble.addEventListener('webviewclosed', function(e) {
    if (!e || !e.response || e.response === 'CANCELLED') return;
    try {
        var config = JSON.parse(decodeURIComponent(e.response));
        if (config.n2yo_key !== undefined) {
            if (config.n2yo_key.length > 0) {
                localStorage.setItem(N2YO_KEY_STORAGE, config.n2yo_key);
                console.log('N2YO key saved');
            } else {
                localStorage.removeItem(N2YO_KEY_STORAGE);
                console.log('N2YO key reset to default');
            }
        }
        getLocationAndFetch();
    } catch(e2) {
        console.log('Config parse error: ' + e2);
    }
});
