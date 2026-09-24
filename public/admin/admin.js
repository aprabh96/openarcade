import { api, ApiError, el, clear } from '../assets/api.js';
import { fmtTime, fmtRange, fmtMoney, fmtDate, fmtDuration, addDays, WEEKDAYS } from '../assets/format.js';

const $ = (id) => document.getElementById(id);

const state = {
  csrf: null,
  user: null,
  venue: null,
  prices: [],
  date: null,
  day: null,
  selected: null,
  panelMode: 'view',
  nowTimer: null,
};

// ----------------------------------------------------------------- helpers

async function call(method, path, body) {
  try {
    return await api(method, path, body, state.csrf);
  } catch (error) {
    if (error instanceof ApiError && error.status === 401) {
      showLogin('Your session ended. Please sign in again.');
    }
    throw error;
  }
}

function flash(message, kind = '') {
  const box = $('flash');
  box.textContent = message;
  box.className = 'notice ' + kind;
  box.classList.remove('hidden');
  clearTimeout(flash.timer);
  flash.timer = setTimeout(() => box.classList.add('hidden'), 4000);
}

function showError(id, error) {
  const box = $(id);
  const fields = error instanceof ApiError ? error.fields : {};
  const lines = Object.entries(fields).map(([k, v]) => `${k}: ${v}`);
  box.textContent = lines.length ? lines.join(' · ') : (error.message || 'Something went wrong.');
  box.classList.remove('hidden');
}

function hideError(id) { $(id).classList.add('hidden'); }

function minutesToTime(minute) { return `${String(Math.floor(minute / 60)).padStart(2, '0')}:${String(minute % 60).padStart(2, '0')}`; }
function timeToMinutes(value) { const [h, m] = value.split(':').map(Number); return h * 60 + m; }
function localToday() {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
}

// ----------------------------------------------------------------- auth

function showLogin(message) {
  $('app').classList.add('hidden');
  $('panel').classList.add('hidden');
  $('login').classList.remove('hidden');
  const box = $('login-error');
  if (message) { box.textContent = message; box.classList.remove('hidden'); } else box.classList.add('hidden');
  $('username').focus();
}

async function login(event) {
  event.preventDefault();
  hideError('login-error');
  try {
    const data = await api('POST', '/api/admin/login', { username: $('username').value.trim(), password: $('password').value });
    enter(data);
    $('password').value = '';
  } catch (error) {
    showError('login-error', error);
  }
}

async function enter(session) {
  state.csrf = session.csrf_token;
  state.user = session.user;
  $('whoami').textContent = session.user.username;
  $('login').classList.add('hidden');
  $('app').classList.remove('hidden');
  await loadVenue();
  state.date = state.date || localToday();
  $('day').value = state.date;
  await loadDay();
}

async function logout() {
  try { await call('POST', '/api/admin/logout'); } catch (error) { /* already out */ }
  state.csrf = null;
  showLogin();
}

// ----------------------------------------------------------------- venue

async function loadVenue() {
  const data = await api('GET', '/api/venue');
  state.venue = data.venue;
  state.prices = data.prices;
  $('venue-name').textContent = data.venue.name;
  document.documentElement.style.setProperty('--brand', data.venue.brand_color);
}

// ----------------------------------------------------------------- day view

async function loadDay() {
  const data = await call('GET', `/api/admin/reservations?date=${encodeURIComponent(state.date)}`);
  state.day = data;
  $('day-hours').textContent = data.hours ? `Open ${fmtTime(data.hours.open_minute)} to ${fmtTime(data.hours.close_minute)}` : 'Closed';
  renderTimeline();
  renderList();
}

function renderTimeline() {
  const box = clear($('timeline'));
  const day = state.day;
  const active = day.reservations.filter((r) => r.status === 'confirmed' || r.status === 'held');
  const openMinute = day.hours ? day.hours.open_minute : Math.min(540, ...active.map((r) => r.start_minute));
  const closeMinute = day.hours ? day.hours.close_minute : Math.max(1320, ...active.map((r) => r.end_minute));
  const span = Math.max(60, closeMinute - openMinute);
  box.style.setProperty('--hours', String(Math.max(1, Math.round(span / 60))));

  const ruler = el('div', { class: 'ruler' });
  for (let m = Math.ceil(openMinute / 60) * 60; m <= closeMinute; m += 60) {
    ruler.append(el('span', { class: 'tick', style: `left:${((m - openMinute) / span) * 100}%`, text: fmtTime(m).replace(':00', '') }));
  }
  box.append(ruler);

  for (const station of day.stations) {
    const track = el('div', { class: 'lane-track' });
    for (const r of active.filter((x) => x.stations.includes(station.number))) {
      const left = ((r.start_minute - openMinute) / span) * 100;
      const width = ((r.end_minute - r.start_minute) / span) * 100;
      track.append(el('button', {
        type: 'button',
        class: `res ${r.status}`,
        style: `left:${Math.max(0, left)}%;width:${Math.max(1.5, Math.min(100 - left, width))}%`,
        title: `${r.first_name} ${r.last_name}, ${fmtRange(r.start_minute, r.duration_minutes)}`,
        onclick: () => openPanel(r.id),
      }, `${fmtTime(r.start_minute)} ${r.first_name} ${r.last_name}`));
    }
    box.append(el('div', { class: 'lane' }, [el('div', { class: 'lane-label', text: station.label }), track]));
  }
  updateNowMarker(openMinute, span);
}

function updateNowMarker(openMinute, span) {
  clearInterval(state.nowTimer);
  const draw = () => {
    for (const old of document.querySelectorAll('.timeline .now')) old.remove();
    if (state.date !== localToday()) return;
    const now = new Date();
    const minute = now.getHours() * 60 + now.getMinutes();
    const pct = ((minute - openMinute) / span) * 100;
    if (pct < 0 || pct > 100) return;
    for (const track of document.querySelectorAll('.timeline .lane-track')) {
      track.append(el('div', { class: 'now', style: `left:${pct}%` }));
    }
  };
  draw();
  state.nowTimer = setInterval(draw, 30000);
}

function renderList() {
  const box = clear($('day-list'));
  const rows = state.day.reservations;
  box.append(el('h2', { text: `${fmtDate(state.date)}: ${rows.length} reservation${rows.length === 1 ? '' : 's'}` }));
  if (!rows.length) { box.append(el('p', { class: 'muted', text: 'Nothing booked yet.' })); return; }
  const table = el('table');
  table.append(el('thead', {}, el('tr', {}, ['Time', 'Guest', 'Stations', 'Total', 'Status'].map((h) => el('th', { text: h })))));
  const tbody = el('tbody');
  for (const r of rows) {
    tbody.append(el('tr', { class: 'status-row', tabindex: '0', onclick: () => openPanel(r.id), onkeydown: (e) => { if (e.key === 'Enter') openPanel(r.id); } }, [
      el('td', { text: fmtRange(r.start_minute, r.duration_minutes) }),
      el('td', {}, [el('div', { text: `${r.first_name} ${r.last_name}` }), el('div', { class: 'small muted', text: `${r.phone}${r.email ? ' · ' + r.email : ''}` })]),
      el('td', { text: r.stations.map((n) => '#' + n).join(', ') }),
      el('td', { text: fmtMoney(r.total_cents, r.currency) + (r.payment_id ? ' paid' : r.total_cents ? ' due' : '') }),
      el('td', {}, el('span', { class: `badge ${r.status}`, text: r.status.replace('_', ' ') })),
    ]));
  }
  table.append(tbody);
  box.append(table);
}

// ----------------------------------------------------------------- reservation panel

function openPanel(id) {
  state.selected = id === null ? null : state.day.reservations.find((r) => r.id === id) || null;
  state.panelMode = id === null ? 'new' : 'view';
  renderPanel();
  $('panel').classList.remove('hidden');
}

function closePanel() {
  $('panel').classList.add('hidden');
  state.selected = null;
}

function renderPanel() {
  const body = clear($('panel-body'));
  const r = state.selected;
  if (state.panelMode === 'new' || state.panelMode === 'edit') {
    $('panel-title').textContent = state.panelMode === 'new' ? 'New booking' : `Edit ${r.confirmation_code}`;
    body.append(reservationForm(r));
    return;
  }
  $('panel-title').textContent = `${r.first_name} ${r.last_name}`;
  const facts = [
    ['Code', r.confirmation_code],
    ['Status', r.status.replace('_', ' ')],
    ['When', `${fmtDate(r.date)}, ${fmtRange(r.start_minute, r.duration_minutes)}`],
    ['Stations', r.stations.map((n) => '#' + n).join(', ')],
    ['Total', `${fmtMoney(r.total_cents, r.currency)}${r.payment_id ? ` (paid via ${r.payment_provider})` : r.total_cents ? ' (due at counter)' : ' (complimentary)'}`],
    ['Phone', r.phone || '—'],
    ['Email', r.email || '—'],
    ['Comments', r.comments || '—'],
    ['Booked', `${r.created_by}, ${r.created_at} UTC`],
  ];
  const dl = el('dl', { class: 'details' });
  for (const [k, v] of facts) dl.append(el('dt', { text: k }), el('dd', { text: v }));
  body.append(dl);

  if (r.status === 'confirmed' || r.status === 'held') {
    body.append(el('div', { class: 'actions' }, [
      el('button', { type: 'button', class: 'btn small secondary', text: 'Edit', onclick: () => { state.panelMode = 'edit'; renderPanel(); } }),
      el('button', { type: 'button', class: 'btn small danger', text: 'Cancel booking', onclick: () => cancelReservation(r) }),
    ]));
  }
}


async function cancelReservation(r) {
  if (!window.confirm(`Cancel ${r.first_name} ${r.last_name}'s booking at ${fmtTime(r.start_minute)}?`)) return;
  try {
    await call('POST', `/api/admin/reservations/${r.id}/cancel`);
    flash('Booking cancelled.', 'success');
    closePanel();
    await loadDay();
  } catch (error) { flash(error.message, 'error'); }
}

function reservationForm(r) {
  const isNew = !r;
  const form = el('form', { class: 'stack', novalidate: true });
  const field = (name, label, input) => el('div', { class: 'field', dataset: { field: name } }, [el('label', { for: `f-${name}`, text: label }), input]);
  const durations = [...new Set(state.prices.map((p) => p.duration_minutes))].sort((a, b) => a - b);
  const durationSelect = el('select', { id: 'f-duration_minutes', name: 'duration_minutes' }, durations.map((d) => el('option', { value: String(d), text: fmtDuration(d), selected: (r ? r.duration_minutes : durations[0]) === d })));
  const stationSelect = el('select', { id: 'f-station_count', name: 'station_count' }, state.day.stations.map((s, i) => el('option', { value: String(i + 1), text: String(i + 1), selected: (r ? r.station_count : 1) === i + 1 })));
  form.append(
    el('div', { class: 'grid-2' }, [
      field('date', 'Date', el('input', { type: 'date', id: 'f-date', name: 'date', value: r ? r.date : state.date, required: true })),
      field('start_minute', 'Start', el('input', { type: 'time', id: 'f-start_minute', name: 'start', value: minutesToTime(r ? r.start_minute : (state.day.hours ? state.day.hours.open_minute : 600)), step: '300', required: true })),
      field('duration_minutes', 'Length', durationSelect),
      field('station_count', 'Stations', stationSelect),
      field('first_name', 'First name', el('input', { type: 'text', id: 'f-first_name', name: 'first_name', value: r ? r.first_name : '', required: true })),
      field('last_name', 'Last name', el('input', { type: 'text', id: 'f-last_name', name: 'last_name', value: r ? r.last_name : '', required: true })),
      field('email', 'Email', el('input', { type: 'email', id: 'f-email', name: 'email', value: r ? r.email : '' })),
      field('phone', 'Phone', el('input', { type: 'tel', id: 'f-phone', name: 'phone', value: r ? r.phone : '' })),
    ]),
    field('comments', 'Comments', el('textarea', { id: 'f-comments', name: 'comments', value: r && r.comments ? r.comments : '' })),
    isNew ? el('label', { class: 'row' }, [el('input', { type: 'checkbox', name: 'complimentary' }), ' Complimentary (no charge)']) : null,
    el('div', { id: 'panel-error', class: 'notice error hidden', role: 'alert' }),
    el('div', { class: 'row spread' }, [
      el('button', { type: 'button', class: 'btn secondary', text: 'Back', onclick: () => { if (isNew) closePanel(); else { state.panelMode = 'view'; renderPanel(); } } }),
      el('button', { type: 'submit', class: 'btn', text: isNew ? 'Create booking' : 'Save changes' }),
    ]),
  );
  form.addEventListener('submit', async (event) => {
    event.preventDefault();
    const body = {
      date: form.date.value,
      start_minute: timeToMinutes(form.start.value || '00:00'),
      duration_minutes: Number(form.duration_minutes.value),
      station_count: Number(form.station_count.value),
      first_name: form.first_name.value.trim(),
      last_name: form.last_name.value.trim(),
      email: form.email.value.trim(),
      phone: form.phone.value.trim(),
      comments: form.comments.value.trim() || null,
    };
    if (isNew) body.complimentary = form.complimentary.checked;
    for (const f of form.querySelectorAll('.field.has-error')) { f.classList.remove('has-error'); const m = f.querySelector('.error'); if (m) m.remove(); }
    form.querySelector('#panel-error').classList.add('hidden');
    try {
      const data = isNew
        ? await call('POST', '/api/admin/reservations', body)
        : await call('PATCH', `/api/admin/reservations/${r.id}`, body);
      flash(isNew ? 'Booking created.' : 'Booking updated.', 'success');
      state.date = data.reservation.date;
      $('day').value = state.date;
      await loadDay();
      state.selected = state.day.reservations.find((x) => x.id === data.reservation.id) || null;
      state.panelMode = 'view';
      renderPanel();
    } catch (error) {
      const fields = error instanceof ApiError ? error.fields : {};
      for (const [name, message] of Object.entries(fields)) {
        const f = form.querySelector(`.field[data-field="${name}"]`);
        if (f) { f.classList.add('has-error'); f.append(el('div', { class: 'error', text: message })); }
      }
      if (!Object.keys(fields).length) {
        const box = form.querySelector('#panel-error');
        box.textContent = error.message;
        box.classList.remove('hidden');
      }
    }
  });
  return form;
}

// ----------------------------------------------------------------- settings

const SETTING_FIELDS = [
  ['venue_name', 'Venue name', 'text'], ['timezone', 'Timezone', 'select'], ['currency', 'Currency (3 letters)', 'text'],
  ['tax_rate_bp', 'Tax rate in basis points (935 = 9.35%)', 'number'], ['slot_step_minutes', 'Start times every (minutes)', 'number'],
  ['buffer_minutes', 'Gap between sessions (minutes)', 'number'], ['min_lead_minutes', 'Minimum notice (minutes)', 'number'],
  ['max_advance_days', 'How far ahead customers can book (days)', 'number'], ['hold_minutes', 'Payment hold (minutes)', 'number'],
  ['notification_email', 'Email new bookings to', 'email'], ['brand_color', 'Brand colour', 'color'], ['logo_url', 'Logo URL (https)', 'url'],
  ['venue_address', 'Address shown to customers', 'text'], ['venue_phone', 'Phone shown to customers', 'tel'],
];

async function loadSettings() {
  const [settings, hours, prices, closures, stations] = await Promise.all([
    call('GET', '/api/admin/settings'), call('GET', '/api/admin/hours'), call('GET', '/api/admin/prices'),
    call('GET', '/api/admin/closures'), call('GET', '/api/admin/stations'),
  ]);
  renderSettings(settings);
  renderHours(hours.weekdays);
  renderPrices(prices.prices);
  renderClosures(closures);
  renderStations(stations.stations);
}

function renderSettings(data) {
  const box = clear($('settings-fields'));
  for (const [key, label, type] of SETTING_FIELDS) {
    let input;
    if (type === 'select') {
      input = el('select', { id: `s-${key}`, name: key }, data.timezones.map((tz) => el('option', { value: tz, text: tz, selected: tz === data.settings[key] })));
    } else {
      input = el('input', { type, id: `s-${key}`, name: key, value: data.settings[key] });
    }
    box.append(el('div', { class: 'field' }, [el('label', { for: `s-${key}`, text: label }), input]));
  }
}

async function saveSettings(event) {
  event.preventDefault();
  hideError('settings-error');
  const settings = {};
  for (const [key] of SETTING_FIELDS) settings[key] = $('settings-form')[key].value;
  try {
    await call('PUT', '/api/admin/settings', { settings });
    flash('Venue settings saved.', 'success');
    await loadVenue();
  } catch (error) { showError('settings-error', error); }
}

function renderHours(weekdays) {
  const tbody = clear($('hours-table').querySelector('tbody'));
  for (const day of weekdays) {
    tbody.append(el('tr', { dataset: { weekday: String(day.weekday) } }, [
      el('td', { text: WEEKDAYS[day.weekday] }),
      el('td', {}, el('input', { type: 'time', name: 'open', value: minutesToTime(day.open_minute), step: '300', 'aria-label': `${WEEKDAYS[day.weekday]} opens` })),
      el('td', {}, el('input', { type: 'time', name: 'close', value: minutesToTime(Math.min(day.close_minute, 1439)), step: '300', 'aria-label': `${WEEKDAYS[day.weekday]} closes` })),
      el('td', {}, el('input', { type: 'checkbox', name: 'closed', checked: day.closed, 'aria-label': `${WEEKDAYS[day.weekday]} closed` })),
    ]));
  }
}

async function saveHours(event) {
  event.preventDefault();
  hideError('hours-error');
  const weekdays = [];
  for (const row of $('hours-table').querySelectorAll('tbody tr')) {
    const close = timeToMinutes(row.querySelector('[name=close]').value);
    weekdays.push({
      weekday: Number(row.dataset.weekday),
      open_minute: timeToMinutes(row.querySelector('[name=open]').value),
      close_minute: close === 1439 ? 1440 : close,
      closed: row.querySelector('[name=closed]').checked,
    });
  }
  try {
    const data = await call('PUT', '/api/admin/hours', { weekdays });
    renderHours(data.weekdays);
    flash('Opening hours saved.', 'success');
  } catch (error) { showError('hours-error', error); }
}

function renderPrices(prices) {
  const tbody = clear($('prices-table').querySelector('tbody'));
  for (const p of prices) {
    tbody.append(el('tr', {}, [
      el('td', { text: p.weekday === -1 ? 'Every day' : WEEKDAYS[p.weekday] }),
      el('td', { text: fmtDuration(p.duration_minutes) }),
      el('td', { text: fmtMoney(p.price_cents, state.venue.currency) }),
      el('td', {}, el('button', { type: 'button', class: 'btn small secondary', text: 'Remove', onclick: async () => {
        try { renderPrices((await call('PUT', '/api/admin/prices', { remove: [{ weekday: p.weekday, duration_minutes: p.duration_minutes }] })).prices); await loadVenue(); } catch (error) { showError('prices-error', error); }
      } })),
    ]));
  }
  const weekdaySelect = clear($('price-weekday'));
  weekdaySelect.append(el('option', { value: '-1', text: 'Every day' }));
  WEEKDAYS.forEach((name, i) => weekdaySelect.append(el('option', { value: String(i), text: name })));
}

async function addPrice(event) {
  event.preventDefault();
  hideError('prices-error');
  const body = { set: [{ weekday: Number($('price-weekday').value), duration_minutes: Number($('price-duration').value), price_cents: Math.round(Number($('price-amount').value) * 100) }] };
  try {
    renderPrices((await call('PUT', '/api/admin/prices', body)).prices);
    await loadVenue();
    flash('Price saved.', 'success');
  } catch (error) { showError('prices-error', error); }
}

function renderClosures(data) {
  const tbody = clear($('closures-table').querySelector('tbody'));
  const rows = [
    ...data.closed_dates.map((c) => ({ date: c.date, detail: `Closed${c.reason ? ': ' + c.reason : ''}`, remove: { remove_closed: [c.date] } })),
    ...data.special_hours.map((s) => ({ date: s.date, detail: `Open ${fmtTime(s.open_minute)} to ${fmtTime(s.close_minute)}`, remove: { remove_special: [s.date] } })),
  ].sort((a, b) => a.date.localeCompare(b.date));
  if (!rows.length) tbody.append(el('tr', {}, el('td', { colspan: '3', class: 'muted', text: 'No upcoming closures or special hours.' })));
  for (const row of rows) {
    tbody.append(el('tr', {}, [
      el('td', { text: fmtDate(row.date) }), el('td', { text: row.detail }),
      el('td', {}, el('button', { type: 'button', class: 'btn small secondary', text: 'Remove', onclick: async () => {
        try { renderClosures(await call('PUT', '/api/admin/closures', row.remove)); } catch (error) { showError('closures-error', error); }
      } })),
    ]));
  }
}

async function addClosure(event) {
  event.preventDefault();
  hideError('closures-error');
  const date = $('closure-date').value;
  const body = $('closure-kind').value === 'closed'
    ? { add_closed: [{ date, reason: $('closure-reason').value.trim() }] }
    : { add_special: [{ date, open_minute: timeToMinutes($('closure-open').value || '10:00'), close_minute: timeToMinutes($('closure-close').value || '22:00') }] };
  try {
    renderClosures(await call('PUT', '/api/admin/closures', body));
    flash('Saved.', 'success');
  } catch (error) { showError('closures-error', error); }
}

function renderStations(stations) {
  $('station-count').value = String(stations.length);
  const box = clear($('station-labels'));
  for (const s of stations) {
    box.append(el('div', { class: 'field' }, [el('label', { for: `st-${s.number}`, text: `Station ${s.number} label` }), el('input', { type: 'text', id: `st-${s.number}`, dataset: { number: String(s.number) }, value: s.label, maxlength: '60' })]));
  }
}

async function saveStations(event) {
  event.preventDefault();
  hideError('stations-error');
  const labels = {};
  for (const input of $('station-labels').querySelectorAll('input')) labels[input.dataset.number] = input.value.trim();
  try {
    const data = await call('PUT', '/api/admin/stations', { count: Number($('station-count').value), labels });
    renderStations(data.stations);
    await loadVenue();
    flash('Stations saved.', 'success');
  } catch (error) { showError('stations-error', error); }
}

// ----------------------------------------------------------------- tabs and boot

function showTab(name) {
  for (const button of document.querySelectorAll('.tab')) button.setAttribute('aria-pressed', String(button.dataset.tab === name));
  $('tab-day').classList.toggle('hidden', name !== 'day');
  $('tab-settings').classList.toggle('hidden', name !== 'settings');
  if (name === 'settings') loadSettings().catch((error) => flash(error.message, 'error'));
}

function changeDay(value) {
  state.date = value;
  $('day').value = value;
  closePanel();
  loadDay().catch((error) => flash(error.message, 'error'));
}

$('login-form').addEventListener('submit', login);
$('logout').addEventListener('click', logout);
$('day').addEventListener('change', (e) => changeDay(e.target.value));
$('prev-day').addEventListener('click', () => changeDay(addDays(state.date, -1)));
$('next-day').addEventListener('click', () => changeDay(addDays(state.date, 1)));
$('today').addEventListener('click', () => changeDay(localToday()));
$('new-booking').addEventListener('click', () => openPanel(null));
$('panel-close').addEventListener('click', closePanel);
$('settings-form').addEventListener('submit', saveSettings);
$('hours-form').addEventListener('submit', saveHours);
$('price-form').addEventListener('submit', addPrice);
$('closure-form').addEventListener('submit', addClosure);
$('closure-kind').addEventListener('change', (e) => {
  const special = e.target.value === 'special';
  $('closure-open').classList.toggle('hidden', !special);
  $('closure-close').classList.toggle('hidden', !special);
  $('closure-reason').classList.toggle('hidden', special);
});
$('stations-form').addEventListener('submit', saveStations);
for (const button of document.querySelectorAll('.tab')) button.addEventListener('click', () => showTab(button.dataset.tab));

(async () => {
  try {
    const session = await api('GET', '/api/admin/me');
    await enter(session);
  } catch (error) {
    showLogin();
  }
})();
