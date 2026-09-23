import { api, ApiError, el, clear } from '../assets/api.js';
import { fmtTime, fmtRange, fmtMoney, fmtDate, fmtDuration, addDays, weekdayOf } from '../assets/format.js';

const $ = (id) => document.getElementById(id);

const state = {
  venue: null,
  prices: [],
  durations: [],
  payment: { mode: 'none' },
  today: null,
  date: null,
  duration: null,
  stations: 1,
  start: null,
  bookingToken: null,
  requestId: null,
  card: null,
  submitting: false,
};

// ----------------------------------------------------------------- pricing

function priceFor(date, duration) {
  const weekday = weekdayOf(date);
  const specific = state.prices.find((p) => p.weekday === weekday && p.duration_minutes === duration);
  const general = state.prices.find((p) => p.weekday === -1 && p.duration_minutes === duration);
  const row = specific || general;
  return row ? row.price_cents : null;
}

function quote() {
  const unit = priceFor(state.date, state.duration);
  if (unit === null) return null;
  const subtotal = unit * state.stations;
  const tax = Math.floor((subtotal * state.venue.tax_rate_bp + 5000) / 10000);
  return { subtotal, tax, total: subtotal + tax };
}

// ----------------------------------------------------------------- step 1

function renderDurations() {
  const box = clear($('durations'));
  const offered = state.durations.filter((d) => priceFor(state.date, d) !== null);
  if (!offered.includes(state.duration)) state.duration = offered[0] || null;
  for (const duration of offered) {
    box.append(el('button', {
      type: 'button',
      class: 'duration',
      'aria-pressed': String(duration === state.duration),
      onclick: () => { state.duration = duration; renderDurations(); loadSlots(); },
    }, [
      el('span', { text: fmtDuration(duration) }),
      el('span', { class: 'price', text: `${fmtMoney(priceFor(state.date, duration), state.venue.currency)} per station` }),
    ]));
  }
}

function renderStations() {
  const select = clear($('stations'));
  for (let n = 1; n <= state.venue.station_count; n++) {
    select.append(el('option', { value: String(n), text: n === 1 ? '1 station' : `${n} stations`, selected: n === state.stations }));
  }
}

let slotRequest = 0;

async function loadSlots() {
  const status = $('slots-status');
  const box = clear($('slots'));
  if (!state.date || !state.duration) { status.textContent = ''; return; }
  const requestId = ++slotRequest;
  status.textContent = 'Checking availability…';
  try {
    const data = await api('GET', `/api/availability?date=${encodeURIComponent(state.date)}&duration=${state.duration}&stations=${state.stations}`);
    if (requestId !== slotRequest) return;
    if (data.closed) {
      status.textContent = `We're closed on ${fmtDate(state.date)}. Please pick another date.`;
      return;
    }
    const open = data.slots.filter((s) => s.free >= state.stations);
    status.textContent = open.length
      ? `Open ${fmtTime(data.open_minute)} to ${fmtTime(data.close_minute)}. Pick a start time:`
      : `No ${fmtDuration(state.duration)} slots for ${state.stations} station${state.stations > 1 ? 's' : ''} on ${fmtDate(state.date)}. Try another date, a shorter session or fewer stations.`;
    for (const slot of data.slots) {
      const available = slot.free >= state.stations;
      box.append(el('button', {
        type: 'button',
        class: 'slot',
        disabled: !available,
        'aria-label': `${fmtTime(slot.start_minute)}, ${available ? slot.free + ' free' : 'unavailable'}`,
        onclick: () => choose(slot.start_minute),
      }, [fmtTime(slot.start_minute), el('span', { class: 'free', text: available ? `${slot.free} free` : 'full' })]));
    }
  } catch (error) {
    if (requestId !== slotRequest) return;
    status.textContent = error instanceof ApiError ? error.message : 'Could not load availability.';
  }
}

function choose(startMinute) {
  state.start = startMinute;
  const q = quote();
  const summary = $('summary');
  summary.textContent = `${fmtDate(state.date)}, ${fmtRange(state.start, state.duration)}, ${state.stations} station${state.stations > 1 ? 's' : ''}`
    + (q ? `. Total ${fmtMoney(q.total, state.venue.currency)}` + (q.tax ? ` (includes ${fmtMoney(q.tax, state.venue.currency)} tax)` : '') : '')
    + (state.payment.mode === 'none' ? '. Pay at the venue.' : '.');
  show('step-who');
  $('first_name').focus();
  if (state.payment.mode === 'square') mountCard();
}

// ----------------------------------------------------------------- payment (Square Web Payments SDK)

async function mountCard() {
  $('payment').classList.remove('hidden');
  if (state.card) return;
  try {
    await loadSquareSdk();
    const payments = window.Square.payments(state.payment.square.application_id, state.payment.square.location_id);
    state.card = await payments.card();
    await state.card.attach('#card-container');
  } catch (error) {
    showFormError('The payment form could not be loaded. Please refresh the page or contact the venue.');
  }
}

function loadSquareSdk() {
  if (window.Square) return Promise.resolve();
  return new Promise((resolve, reject) => {
    const script = document.createElement('script');
    script.src = state.payment.square.environment === 'production'
      ? 'https://web.squarecdn.com/v1/square.js'
      : 'https://sandbox.web.squarecdn.com/v1/square.js';
    script.onload = resolve;
    script.onerror = () => reject(new Error('sdk'));
    document.head.append(script);
  });
}

async function paymentToken() {
  if (state.payment.mode !== 'square') return null;
  if (!state.card) throw new ApiError(0, 'card', 'The payment form is not ready yet.');
  const result = await state.card.tokenize();
  if (result.status !== 'OK') {
    const detail = result.errors && result.errors[0] ? result.errors[0].message : 'Please check the card details.';
    throw new ApiError(0, 'card', detail);
  }
  return result.token;
}

// ----------------------------------------------------------------- step 2

async function refreshBookingToken() {
  const data = await api('GET', '/api/booking-token');
  state.bookingToken = data.token;
}

function clearFieldErrors() {
  for (const field of document.querySelectorAll('.field.has-error')) {
    field.classList.remove('has-error');
    const message = field.querySelector('.error');
    if (message) message.remove();
  }
  $('form-error').classList.add('hidden');
}

function showFieldErrors(fields) {
  for (const [name, message] of Object.entries(fields)) {
    const field = document.querySelector(`.field[data-field="${name}"]`);
    if (!field) continue;
    field.classList.add('has-error');
    field.append(el('div', { class: 'error', text: message }));
  }
}

function showFormError(message) {
  const box = $('form-error');
  box.textContent = message;
  box.classList.remove('hidden');
}

async function submit(event) {
  event.preventDefault();
  if (state.submitting) return;
  clearFieldErrors();
  const form = $('details');
  const body = {
    booking_token: state.bookingToken,
    request_id: state.requestId,
    date: state.date,
    start_minute: state.start,
    duration_minutes: state.duration,
    station_count: state.stations,
    first_name: form.first_name.value.trim(),
    last_name: form.last_name.value.trim(),
    email: form.email.value.trim(),
    phone: form.phone.value.trim(),
    comments: form.comments.value.trim() || null,
  };
  // One id per booking attempt: a retry after a lost answer reuses it, so the card is never charged twice.
  if (!state.requestId) state.requestId = crypto.randomUUID();
  body.request_id = state.requestId;
  state.submitting = true;
  $('submit').disabled = true;
  $('submit').textContent = 'Booking…';
  try {
    body.payment_token = await paymentToken();
    const data = await api('POST', '/api/reservations', body);
    state.requestId = null;
    showDone(data.reservation);
  } catch (error) {
    if (error instanceof ApiError && (error.code === 'card' || Object.keys(error.fields).length)) state.requestId = null;
    if (error instanceof ApiError && error.code === 'card') {
      showFieldErrors({ payment_token: error.message });
    } else if (error instanceof ApiError && Object.keys(error.fields).length) {
      showFieldErrors(error.fields);
    } else if (error instanceof ApiError && error.code === 'slot_unavailable') {
      state.requestId = null;
      show('step-when');
      await loadSlots();
      $('slots-status').textContent = `${error.message} ${$('slots-status').textContent}`;
    } else if (error instanceof ApiError && error.code === 'booking_expired') {
      await refreshBookingToken();
      showFormError('Your session was refreshed. Please press Confirm again.');
    } else {
      // Keep the request id only when the outcome is unknown; anything else starts a fresh attempt.
      if (!(error instanceof ApiError) || !['payment_unknown', 'network'].includes(error.code)) state.requestId = null;
      showFormError(error.message || 'Something went wrong. Please try again.');
    }
  } finally {
    state.submitting = false;
    $('submit').disabled = false;
    $('submit').textContent = 'Confirm booking';
  }
}

function showDone(reservation) {
  $('done-code').textContent = reservation.confirmation_code;
  $('done-when').textContent = `${fmtDate(reservation.date)}, ${fmtRange(reservation.start_minute, reservation.duration_minutes)}`;
  $('done-stations').textContent = reservation.stations.map((n) => `#${n}`).join(', ');
  $('done-total').textContent = fmtMoney(reservation.total_cents, reservation.currency)
    + (reservation.total_cents > 0 && state.payment.mode === 'none' ? ' (pay at the venue)' : reservation.total_cents > 0 ? ' (paid)' : '');
  $('done-where').textContent = [state.venue.address, state.venue.phone].filter(Boolean).join(' · ') || state.venue.name;
  show('step-done');
}

// ----------------------------------------------------------------- boot

function show(step) {
  for (const id of ['step-when', 'step-who', 'step-done']) $(id).classList.toggle('hidden', id !== step);
  window.scrollTo({ top: 0, behavior: 'smooth' });
}

async function boot() {
  try {
    const data = await api('GET', '/api/venue');
    state.venue = data.venue;
    state.prices = data.prices;
    state.durations = data.durations;
    state.payment = data.payment;
    state.today = data.today;
    document.title = `Book a session at ${data.venue.name}`;
    $('venue-name').textContent = data.venue.name;
    $('tz').textContent = data.venue.timezone.replace('_', ' ');
    document.documentElement.style.setProperty('--brand', data.venue.brand_color);
    if (data.venue.logo_url) {
      const logo = $('logo');
      logo.src = data.venue.logo_url;
      logo.alt = data.venue.name;
      logo.classList.remove('hidden');
    }
    const dateInput = $('date');
    dateInput.min = data.today;
    dateInput.max = addDays(data.today, data.venue.max_advance_days);
    dateInput.value = data.today;
    state.date = data.today;
    dateInput.addEventListener('change', () => { state.date = dateInput.value; renderDurations(); loadSlots(); });
    renderStations();
    $('stations').addEventListener('change', (event) => { state.stations = Number(event.target.value); loadSlots(); });
    renderDurations();
    await refreshBookingToken();
    $('loading').classList.add('hidden');
    show('step-when');
    loadSlots();
  } catch (error) {
    $('loading').classList.add('hidden');
    const fatal = $('fatal');
    fatal.textContent = error instanceof ApiError ? error.message : 'The booking page could not be loaded.';
    fatal.classList.remove('hidden');
  }
}

$('details').addEventListener('submit', submit);
$('back').addEventListener('click', () => show('step-when'));
$('again').addEventListener('click', () => { state.requestId = null; $('details').reset(); clearFieldErrors(); show('step-when'); loadSlots(); });
boot();
