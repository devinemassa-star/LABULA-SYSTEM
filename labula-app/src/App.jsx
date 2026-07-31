import { useEffect, useState, useRef } from 'react'
import './App.css'

const FIREBASE_HOST = import.meta.env.VITE_FIREBASE_HOST || 'https://labula-5bb9c-default-rtdb.firebaseio.com/'
const FIREBASE_TOKEN = import.meta.env.VITE_FIREBASE_TOKEN || '57P1q4qN3rmehxkLwljEburFrEXbmGx1Q64GI9a7'
const BASE_PATH = import.meta.env.VITE_FIREBASE_BASE_PATH || 'labula_secret_2024/LABULA'

function buildUrl(path) {
  const host = FIREBASE_HOST.endsWith('/') ? FIREBASE_HOST : FIREBASE_HOST + '/'
  return `${host}${path}.json?auth=${FIREBASE_TOKEN}`
}

function App() {
  const [data, setData] = useState({})
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(null)
  const [toggling, setToggling] = useState(false)
  const [togglingFan, setTogglingFan] = useState(false)
  const [togglingVent, setTogglingVent] = useState(false)
  const [connected, setConnected] = useState(false)
  const [notificationsEnabled, setNotificationsEnabled] = useState(false)
  const [rawData, setRawData] = useState(null)
  const intervalRef = useRef(null)
  const lastStatusRef = useRef(null)

  async function fetchData() {
    try {
      const url = buildUrl(BASE_PATH)
      const res = await fetch(url)
      if (!res.ok) throw new Error('Network response was not ok')
      const json = await res.json()
      setData(json || {})
      setRawData(json)
      setError(null)
      setConnected(true)
      setLoading(false)
    } catch (err) {
      setError(err.message)
      setConnected(false)
      setLoading(false)
    }
  }

  // Request notification permission
  async function requestNotificationPermission() {
    if (!('Notification' in window)) {
      return
    }
    if (Notification.permission === 'granted') {
      setNotificationsEnabled(true)
      return
    }
    if (Notification.permission !== 'denied') {
      try {
        const permission = await Notification.requestPermission()
        setNotificationsEnabled(permission === 'granted')
      } catch (e) {
        // Notification not supported in this context
      }
    }
  }

  // Send browser notification
  function sendNotification(title, body) {
    if (!('Notification' in window) || Notification.permission !== 'granted') {
      return
    }
    try {
      if (document.hidden) {
        new Notification(title, {
          body,
          icon: '/labula.png',
          badge: '/labula.png',
          vibrate: [200, 100, 200],
          requireInteraction: true,
        })
      }
    } catch (e) {
      // Notification failed silently
    }
  }

  // Check for alarm status changes and send notifications
  useEffect(() => {
    const status = data?.status || 'UNKNOWN'
    const gasLevel = data?.gasLevel ?? 0
    const previousStatus = lastStatusRef.current

// Request notification permission on first load only
  useEffect(() => {
    if (notificationsEnabled === false && 'Notification' in window && Notification.permission === 'default') {
      requestNotificationPermission()
    }
  }, [])

    // Send notification on status change
    if (previousStatus && previousStatus !== status) {
      if (status === 'ALARM') {
        sendNotification(
          '🚨 LABULA GAS ALARM',
          `Gas level critical: ${gasLevel}/4095. Fan and vent activated automatically.`
        )
      } else if (status === 'MANUAL_OVERRIDE') {
        sendNotification(
          '🔧 LABULA Manual Override',
          'Manual override is now active. Check the app for details.'
        )
      } else if (status === 'NORMAL' && previousStatus !== 'NORMAL') {
        sendNotification(
          '✅ LABULA Back to Normal',
          'Gas levels are normal. All systems operational.'
        )
      }
    }

    lastStatusRef.current = status
  }, [data, notificationsEnabled])

  useEffect(() => {
    fetchData()
    intervalRef.current = setInterval(fetchData, 2000)
    return () => clearInterval(intervalRef.current)
  }, [])

  async function setManualOverride(value) {
    setToggling(true)
    try {
      const url = buildUrl(`${BASE_PATH}/control/manualOverride`)
      const res = await fetch(url, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(value),
      })
      if (!res.ok) throw new Error('Failed to update override')
      await fetchData()
    } catch (err) {
      setError(err.message)
    } finally {
      setToggling(false)
    }
  }

  async function setFan(value) {
    setTogglingFan(true)
    try {
      const url = buildUrl(`${BASE_PATH}/fanOn`)
      const res = await fetch(url, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(value),
      })
      if (!res.ok) throw new Error('Failed to update fan')
      await fetchData()
    } catch (err) {
      setError(err.message)
    } finally {
      setTogglingFan(false)
    }
  }

  async function setVent(value) {
    setTogglingVent(true)
    try {
      const url = buildUrl(`${BASE_PATH}/ventOpen`)
      const res = await fetch(url, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(value),
      })
      if (!res.ok) throw new Error('Failed to update vent')
      await fetchData()
    } catch (err) {
      setError(err.message)
    } finally {
      setTogglingVent(false)
    }
  }

  const gas = data?.gasLevel ?? 0
  const status = data?.status ?? 'UNKNOWN'
  const fanOn = data?.fanOn === 'true' || data?.fanOn === true
  const ventOpen = data?.ventOpen === 'true' || data?.ventOpen === true
  const manualOverride = data?.control?.manualOverride === true || data?.control?.manualOverride === 'true'
  const switchState = data?.control?.switchState ?? 'UNKNOWN'
  const gasPercent = Math.min(100, Math.round((gas / 4095) * 100))

  return (
    <div className="app">
      <header className="header">
        <div className="header-left">
          <h1>LABULA</h1>
          <p className="subtitle">Gas Monitoring & Control System</p>
        </div>
        <div className={`connection-status ${connected ? 'online' : 'offline'}`}>
          <span className="dot" />
          <span className="label">{connected ? 'Connected' : 'Offline'}</span>
        </div>
      </header>

      <main>
        <section className="card hero">
          <div className="hero-header">
            <h2>Gas Level</h2>
            <span className={`status-badge ${status.toLowerCase()}`}>{status.replace(/_/g, ' ')}</span>
          </div>
          <div className="gauge-section">
            <div className="gauge">
              <div
                className="gauge-fill"
                style={{ width: `${gasPercent}%` }}
              />
            </div>
            <div className="gas-readout">
              <span className="gas-number">{gas}</span>
              <span className="gas-unit">/ 4095</span>
            </div>
          </div>
          <div className="meta-row">
            <span className="meta-item">
              <span className="meta-label">Switch</span>
              <span className={`meta-value ${switchState === 'PRESSED' ? 'active' : ''}`}>{switchState}</span>
            </span>
            <span className="meta-item">
              <span className="meta-label">Threshold</span>
              <span className="meta-value">1500</span>
            </span>
            <span className="meta-item">
              <span className="meta-label">Notifications</span>
              <span className={`meta-value ${notificationsEnabled ? 'active' : ''}`}>
                {notificationsEnabled ? 'Enabled' : 'Click app to enable'}
              </span>
            </span>
          </div>
        </section>

        <div className="controls-grid">
          <section className="card control">
            <div className="control-header">
              <div className="control-icon">🌀</div>
              <h3>Fan</h3>
            </div>
            <p className="control-state">
              <span className={`state-indicator ${fanOn ? 'on' : 'off'}`} />
              {fanOn ? 'ON' : 'OFF'}
            </p>
            <div className="button-group">
              <button
                onClick={() => setFan(true)}
                disabled={togglingFan || fanOn}
                className={`btn ${fanOn ? '' : 'primary'}`}
              >
                {togglingFan ? '...' : 'Turn ON'}
              </button>
              <button
                onClick={() => setFan(false)}
                disabled={togglingFan || !fanOn}
                className={`btn ${!fanOn ? '' : 'danger'}`}
              >
                {togglingFan ? '...' : 'Turn OFF'}
              </button>
            </div>
          </section>

          <section className="card control">
            <div className="control-header">
              <div className="control-icon">🪟</div>
              <h3>Vent</h3>
            </div>
            <p className="control-state">
              <span className={`state-indicator ${ventOpen ? 'on' : 'off'}`} />
              {ventOpen ? 'OPEN' : 'CLOSED'}
            </p>
            <div className="button-group">
              <button
                onClick={() => setVent(true)}
                disabled={togglingVent || ventOpen}
                className={`btn ${ventOpen ? '' : 'primary'}`}
              >
                {togglingVent ? '...' : 'Open'}
              </button>
              <button
                onClick={() => setVent(false)}
                disabled={togglingVent || !ventOpen}
                className={`btn ${!ventOpen ? '' : 'danger'}`}
              >
                {togglingVent ? '...' : 'Close'}
              </button>
            </div>
          </section>

          <section className="card control full-width">
            <div className="control-header">
              <div className="control-icon">🔧</div>
              <h3>Manual Override</h3>
            </div>
            <p className="control-state">
              <span className={`state-indicator ${manualOverride ? 'on' : 'off'}`} />
              {manualOverride ? 'ACTIVE' : 'OFF'}
            </p>
            <div className="button-group">
              <button
                onClick={() => setManualOverride(true)}
                disabled={toggling || manualOverride}
                className={`btn ${manualOverride ? '' : 'primary'}`}
              >
                {toggling ? '...' : 'Activate'}
              </button>
              <button
                onClick={() => setManualOverride(false)}
                disabled={toggling || !manualOverride}
                className={`btn ${!manualOverride ? '' : 'danger'}`}
              >
                {toggling ? '...' : 'Release'}
              </button>
            </div>
          </section>
        </div>

        <section className="card meta">
          <div className="meta-content">
            <div className="meta-item">
              <span className="meta-label">Last Update</span>
              <span className="meta-value">{loading ? 'loading...' : new Date().toLocaleTimeString()}</span>
            </div>
            <div className="meta-item">
              <span className="meta-label">Refresh</span>
              <span className="meta-value">2s</span>
            </div>
          </div>
          {error && <p className="error">Error: {error}</p>}
        </section>

        {rawData && (
          <section className="card meta">
            <div className="meta-content">
              <div className="meta-item" style={{ flex: 1 }}>
                <span className="meta-label">Raw Firebase Data</span>
                <pre className="meta-value" style={{ fontSize: '11px', whiteSpace: 'pre-wrap', wordBreak: 'break-all' }}>
                  {JSON.stringify(rawData, null, 2)}
                </pre>
              </div>
            </div>
          </section>
        )}
      </main>

      <footer>
        <small>LABULA • Firebase Realtime Database</small>
      </footer>
    </div>
  )
}

export default App
