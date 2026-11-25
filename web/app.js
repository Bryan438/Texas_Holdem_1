// The web client: a page that sits at the table like any other player.
// It talks to thserver over a WebSocket with the same JSON messages the
// terminal client sends as lines (see the README's Protocol section), and
// draws whatever the last "state" message says.

import { cardParts, seatPosition, chipPlace, raiseSizes, clampRaise, decisionKey, actionAsEvent, actionsByStreet,
         boardOn, formatChips, formatSeconds } from './logic.js';
import { translate, handName, eventPieces, errorText } from './i18n.js';

const $ = (id) => document.getElementById(id);

// Browser storage may be switched off; the game works without it.
function remembered(storage, key) {
  try { return storage.getItem(key); } catch { return null; }
}
function remember(storage, key, value) {
  try { storage.setItem(key, value); } catch { /* play on without it */ }
}

// The log and the last hand survive a reload of the tab, like the seat does.
function recalled(key, fallback) {
  try { return JSON.parse(remembered(sessionStorage, key)) || fallback; } catch { return fallback; }
}

const S = {
  lang: remembered(localStorage, 'th.lang') || ((navigator.language || '').toLowerCase().startsWith('zh') ? 'zh' : 'en'),
  ws: null,
  online: false,
  wasOnline: false,
  joined: false,
  wantName: remembered(sessionStorage, 'th.name') || '',   // set: join as soon as connected
  wantTable: Number(remembered(sessionStorage, 'th.table')) || 0,   // ...at this table
  wantPassword: remembered(sessionStorage, 'th.pw') || '',
  creating: null,              // a table we asked for: sit down there once it is open
  lobby: null,                 // the last list of tables
  joinError: null,
  state: null,
  log: remembered(sessionStorage, 'th.name') ? recalled('th.log', []) : [],
  summary: remembered(sessionStorage, 'th.name') ? recalled('th.summary', null) : null,
  wasOver: false,
  raiseTo: null,
  decision: '',
  turnEndsAt: 0,
  nextHandAt: 0,
  voteEndsAt: 0,
  noticeTimer: 0,
  drawn: null,                 // the state the table was last drawn from, for motion
  dealing: null,               // { hand, at }: cards of this hand are on their way
};

const t = (key, params) => translate(S.lang, key, params);

// Build an element. Text always goes in as text, never as HTML: names come
// from other players.
function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value == null || value === false) continue;
    if (key === 'class') node.className = value;
    else if (key.startsWith('on')) node.addEventListener(key.slice(2), value);
    else node.setAttribute(key, value === true ? '' : value);
  }
  for (const child of children.flat()) {
    if (child == null || child === false) continue;
    node.append(child.nodeType ? child : document.createTextNode(String(child)));
  }
  return node;
}

// Replace a node's children, skipping the nulls that stand for "nothing here"
// (replaceChildren alone would print them as the word "null").
function fill(node, ...children) {
  node.replaceChildren(...children.flat().filter((child) => child != null && child !== false));
}

// A round picture with the name's first letter, in a colour that stays the
// same for that name. Bots are grey.
const AVATAR_COLORS = ['#ff3b30', '#ff9500', '#34c759', '#00c7be', '#30b0c7', '#007aff',
                       '#5856d6', '#af52de', '#ff2d55', '#a2845e'];
function avatarEl(seat) {
  const letter = ([...seat.name.trim()][0] || '?').toUpperCase();
  let hash = 0;
  for (const ch of seat.name) hash = (hash * 31 + ch.codePointAt(0)) >>> 0;
  return el('div', { class: 'avatar' + (seat.bot ? ' bot' : ''), 'aria-hidden': 'true',
                     style: seat.bot ? null : `--avatar:${AVATAR_COLORS[hash % AVATAR_COLORS.length]}` }, letter);
}

function cardEl(code, size = '') {
  const parts = cardParts(code);
  if (!parts) return el('div', { class: `card ${size} ${code === 'back' ? 'back' : 'slot'}` });
  return el('div', { class: `card ${size} ${parts.red ? 'red' : ''}`, 'aria-label': parts.rank + parts.suit },
            el('span', { class: 'rank' }, parts.rank), el('span', { class: 'suit' }, parts.suit));
}

function inlineCards(codes) {
  return codes.map((code) => {
    const parts = cardParts(code);
    return parts ? el('span', { class: 'inline-card' + (parts.red ? ' red' : '') }, parts.rank + parts.suit) : code;
  });
}

// An event's words, with cards drawn where the sentence names them.
function eventNodes(event) {
  return eventPieces(S.lang, event).flatMap((piece) => (typeof piece === 'string' ? [piece] : inlineCards(piece.cards)));
}

// ---------------------------------------------------------------- network

function connect() {
  const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
  const ws = new WebSocket(`${scheme}://${location.host}/ws`);
  S.ws = ws;
  ws.onopen = () => {
    S.online = true;
    S.wasOnline = true;
    renderConnection();
    // Back after a dropped connection (or a reload): take the seat again. Until
    // the table says welcome, an error means the seat is gone (the table
    // closed, the server restarted): back to the lobby.
    if (S.wantName && S.wantTable) {
      S.rejoining = true;
      sendJoin();
    }
  };
  ws.onmessage = (message) => {
    let data;
    try { data = JSON.parse(message.data); } catch { return; }
    if (data && typeof data === 'object') onMessage(data);
  };
  ws.onclose = () => {
    if (S.ws !== ws) return;
    S.ws = null;
    S.online = false;
    renderConnection();
    setTimeout(connect, 2000);   // the seat is kept: rejoining with the token takes it back
  };
}

function send(message) {
  if (S.ws && S.ws.readyState === WebSocket.OPEN) S.ws.send(JSON.stringify(message));
}

// A seat's token is per table: the same name can sit at another table.
const tokenKey = (table, name) => `th.token.${location.host}.${table}.${name}`;

function sendJoin() {
  const message = { type: 'join', name: S.wantName, table: S.wantTable };
  const token = remembered(localStorage, tokenKey(S.wantTable, S.wantName));
  if (token) message.token = token;
  if (S.wantPassword) message.password = S.wantPassword;
  send(message);
}

// Back in the lobby: a reload must not sit down again.
function forgetTable() {
  S.wantTable = 0;
  S.wantPassword = '';
  for (const key of ['th.name', 'th.table', 'th.pw']) {
    try { sessionStorage.removeItem(key); } catch { /* nothing to forget */ }
  }
}

function onMessage(m) {
  if (m.type === 'welcome') {
    S.rejoining = false;
    S.joined = true;
    S.joinError = null;
    S.wantName = m.name;
    remember(localStorage, tokenKey(S.wantTable, m.name), m.token);
    remember(localStorage, 'th.name', m.name);
    // A reload of this tab sits down at the same table again.
    remember(sessionStorage, 'th.name', m.name);
    remember(sessionStorage, 'th.table', String(S.wantTable));
    remember(sessionStorage, 'th.pw', S.wantPassword);
    for (const id of ['create', 'password']) if ($(id).open) $(id).close();
    renderScreen();
  }
  else if (m.type === 'state') {
    S.state = m;
    S.turnEndsAt = m.turn_time_left == null ? 0 : Date.now() + m.turn_time_left * 1000;
    S.nextHandAt = m.next_hand_in == null ? 0 : Date.now() + m.next_hand_in * 1000;
    S.voteEndsAt = m.vote ? Date.now() + m.vote.seconds_left * 1000 : 0;
    const decision = decisionKey(m);
    if (decision !== S.decision) {
      S.decision = decision;
      const sizes = raiseSizes(m);
      S.raiseTo = sizes ? sizes.min : null;
    }
    renderGame();
    // The moment the game ends, show how it went.
    if (m.game_over && !S.wasOver) showStandings();
    if (!m.game_over && $('standings').open) $('standings').close();
    S.wasOver = Boolean(m.game_over);
  }
  else if (m.type === 'event') {
    S.log.push(m);
    if (S.log.length > 400) S.log.splice(0, S.log.length - 400);
    remember(sessionStorage, 'th.log', JSON.stringify(S.log));
    renderLog(m);
    if (m.kind === 'new_game') {
      // The last hand of the game before is not this game's to review.
      S.summary = null;
      remember(sessionStorage, 'th.summary', 'null');
      renderStatic();
    }
  }
  else if (m.type === 'hand_summary') {
    S.summary = m;
    remember(sessionStorage, 'th.summary', JSON.stringify(m));
    renderStatic();
  }
  else if (m.type === 'left') {
    // Up from the table (by choice, removed, or the table closed): back to
    // the lobby. The token is kept: sitting down again under this name brings
    // the chips back.
    S.joined = false;
    S.state = null;
    S.drawn = null;
    S.wasOver = false;
    S.joinError = m.reason === 'closed' ? { text: t('left.closed') }
                : m.reason !== 'kicked' ? null
                : { text: m.by ? t('left.kicked.by', { by: m.by }) : t('left.kicked') };
    forgetTable();
    for (const dialog of document.querySelectorAll('dialog[open]')) dialog.close();
    renderScreen();
  }
  else if (m.type === 'lobby') {
    S.lobby = m;
    if (!S.joined) renderLobby();
  }
  else if (m.type === 'table_created') {
    // We asked for it: sit down there, as its host.
    if (S.creating) {
      const password = S.creating.password;
      S.creating = null;
      sitAt(m.table, password);
    }
  }
  else if (m.type === 'error') {
    if (!S.joined || S.rejoining) {
      S.rejoining = false;
      S.joined = false;
      S.state = null;
      S.joinError = m;
      S.creating = null;
      forgetTable();
      renderScreen();
    }
    else {
      notice(errorText(S.lang, m));
    }
  }
}

// ---------------------------------------------------------------- drawing

function renderConnection() {
  const conn = $('conn');
  conn.className = 'conn ' + (S.online ? 'online' : S.wasOnline ? 'offline' : '');
  conn.textContent = t(S.online ? 'conn.online' : S.wasOnline ? 'conn.offline' : 'conn.connecting');
}

// Everything whose words depend only on the language.
function renderStatic() {
  document.documentElement.lang = S.lang === 'zh' ? 'zh-CN' : 'en';
  $('title').textContent = t('title');
  $('lang').textContent = t('lang.switch');
  $('lang').setAttribute('aria-label', t('lang.switch.label'));
  $('lobby-title').textContent = t('lobby.title');
  $('join-label').textContent = t('join.name');
  $('create-open').textContent = t('create.open');
  $('join-hint').textContent = t('lobby.hint');
  $('log-title').textContent = t('log.title');
  $('summary-button').textContent = t('summary.button');
  $('summary-button').hidden = !S.summary;
  $('end-game').textContent = t('manage.end');
  $('new-game').textContent = t('manage.new');
  $('leave').textContent = t('manage.leave');
  renderConnection();
}

function renderScreen() {
  $('join').hidden = S.joined;
  $('game').hidden = !S.joined;
  if (!S.joined) {
    $('manage').hidden = true;
    document.title = t('title');
  }
  $('join-error').textContent = !S.joinError ? '' : S.joinError.text || errorText(S.lang, S.joinError);
  if (S.joined) renderGame();
  else renderLobby();
}

// ---------------------------------------------------------------- the lobby

// The main table keeps the server's default name in English; the page words it.
const tableName = (id, name) => (id === 1 && name === 'Main table' ? t('table.main.name') : name);

function renderLobby() {
  const tables = (S.lobby && S.lobby.tables) || [];
  if (tables.length === 0) {
    fill($('tables'), el('li', { class: 'hint' }, t('lobby.empty')));
  }
  else {
    fill($('tables'), tables.map((tb) => el('li', { class: 'table-row' },
      el('div', { class: 'ticon ' + (tb.main ? 'main' : 'n' + (tb.id % 4)), 'aria-hidden': 'true' },
         tb.main ? '♠' : ['♣', '♥', '♦', '♠'][tb.id % 4]),
      el('div', { class: 'tinfo' },
        el('div', { class: 'tname' }, tableName(tb.id, tb.name),
           tb.main && tableName(tb.id, tb.name) === tb.name && el('span', { class: 'tag' }, t('table.main')),
           tb.private && el('span', { class: 'tag' }, t('table.private')),
           tb.playing && el('span', { class: 'tag playing' }, t('table.playing'))),
        el('div', { class: 'tmeta' }, [
          t('table.blinds', { sb: tb.blinds[0], bb: tb.blinds[1] }),
          t('table.chips', { n: formatChips(tb.chips) }),
          t('table.people', { n: tb.seats, max: tb.max }),
          tb.host ? t('table.host', { name: tb.host }) : null,
        ].filter(Boolean).join(' · '))),
      el('button', { class: 'get', type: 'button', disabled: tb.seats >= tb.max,
                     onclick: () => enterTable(tb) }, t('table.enter')))));
  }
  $('create-open').disabled = Boolean(S.lobby && !S.lobby.can_create);
}

// The name in the box, remembered; null (and a word why) when it is empty.
function myName() {
  const name = $('name').value.trim();
  if (!name) {
    S.joinError = { text: t('name.need') };
    renderScreen();
    $('name').focus();
    return null;
  }
  remember(localStorage, 'th.name', name);
  return name;
}

function enterTable(tb) {
  if (!myName()) return;
  if (tb.private) askPassword(tb);
  else sitAt(tb.id, '');
}

function sitAt(id, password) {
  S.joinError = null;
  S.wantName = myName() || S.wantName;
  S.wantTable = id;
  S.wantPassword = password;
  // Sitting down by hand is a fresh start: what an earlier table logged is gone.
  S.log = [];
  S.summary = null;
  remember(sessionStorage, 'th.log', '[]');
  remember(sessionStorage, 'th.summary', 'null');
  renderLog();
  renderStatic();
  $('join-error').textContent = '';
  if (S.online) sendJoin();   // otherwise it is sent as soon as the connection opens
}

function askPassword(tb) {
  const box = $('password');
  const input = el('input', { id: 'table-password', type: 'password', maxlength: 32, autocomplete: 'off' });
  const go = () => { if (input.value) sitAt(tb.id, input.value); };
  input.addEventListener('keydown', (e) => { if (e.key === 'Enter') go(); });
  fill(box,
    el('div', { class: 'head' },
      el('h2', {}, t('password.title', { name: tableName(tb.id, tb.name) })),
      el('button', { class: 'quiet', type: 'button', onclick: () => box.close() }, t('summary.close'))),
    el('div', { class: 'body form-grid' }, input),
    el('div', { class: 'foot' }, el('button', { class: 'primary', type: 'button', onclick: go }, t('password.button'))));
  box.showModal();
  input.focus();
}

const BLINDS = [[1, 2], [2, 5], [5, 10]];
const DEPTHS = [50, 100, 200];   // starting chips, in big blinds

function showCreate() {
  const name = myName();
  if (!name) return;
  const box = $('create');
  const tname = el('input', { id: 'create-name', maxlength: 24, value: t('create.default', { name }) });
  const blinds = el('select', { id: 'create-blinds' },
    BLINDS.map(([sb, bb], i) => el('option', { value: i }, `${sb}/${bb}`)));
  const chips = el('select', { id: 'create-chips' });
  const fillChips = () => {
    const bb = BLINDS[Number(blinds.value)][1];
    fill(chips, DEPTHS.map((d) => el('option', { value: d * bb, selected: d === 100 },
      t('create.chips.choice', { n: formatChips(d * bb), bb: d }))));
  };
  blinds.addEventListener('change', fillChips);
  fillChips();
  const password = el('input', { id: 'create-password', type: 'password', maxlength: 32, autocomplete: 'new-password' });
  const go = () => {
    const [sb, bb] = BLINDS[Number(blinds.value)];
    S.creating = { password: password.value };
    S.joinError = null;
    send({ type: 'create_table', name: tname.value.trim(), blinds: [sb, bb], chips: Number(chips.value),
           password: password.value });
  };
  fill(box,
    el('div', { class: 'head' },
      el('h2', {}, t('create.title')),
      el('button', { class: 'quiet', type: 'button', onclick: () => box.close() }, t('summary.close'))),
    el('div', { class: 'body form-grid' },
      el('label', { for: 'create-name' }, t('create.name')), tname,
      el('label', { for: 'create-blinds' }, t('create.blinds')), blinds,
      el('label', { for: 'create-chips' }, t('create.chips')), chips,
      el('label', { for: 'create-password' }, t('create.password')), password),
    el('div', { class: 'foot' }, el('button', { class: 'primary', type: 'button', onclick: go }, t('create.button'))));
  box.showModal();
}

function renderAll() {
  renderStatic();
  renderScreen();
  renderLog();
  if ($('summary').open) renderSummary();
  if ($('standings').open) renderStandings();
  if ($('bots').open) renderAddBots();
}

function renderGame() {
  const st = S.state;
  const yours = Boolean(st && st.actions);
  document.title = (yours ? '● ' + t('turn.yours') + ' · ' : '') + t('title');
  if (!st) return;
  // Anyone seated may leave, or ask to end or restart (alone with bots it
  // happens at once; with other people they all have to agree).
  $('manage').hidden = !st.seats[st.you];
  $('end-game').hidden = Boolean(st.game_over);
  $('meta').textContent = (st.table ? tableName(st.table.id, st.table.name) + ' · ' : '') +
                          (st.hand > 0 ? t('meta.hand', { n: st.hand }) + ' · ' : '') +
                          t('meta.blinds', { sb: st.blinds[0], bb: st.blinds[1] });
  // A new state (not the same one drawn again, say in another language):
  // what changed since the last one is shown moving.
  const fresh = st !== S.drawn;
  const before = fresh ? chipsBefore() : null;
  renderVote(st);
  renderSeats(st);
  renderCenter(st);
  renderMine(st);
  renderActions(st);
  if (fresh) {
    animateChange(S.drawn, st, before);
    S.drawn = st;
  }
  // Every redraw makes new cards: those still on their way carry on.
  if (S.dealing && S.dealing.hand === st.hand && st.stage !== 'waiting') animateDeal(st, S.dealing.at);
  tick();
}

function renderSeats(st) {
  const live = st.stage !== 'waiting';
  const winners = live ? [] : (st.result || []).map((r) => r.seat);
  const nodes = [];
  for (const seat of st.seats) {
    const at = seatPosition(seat.seat, st.you, st.seats.length);
    const mine = seat.seat === st.you;
    // Disconnected in the middle of a hand: still holding cards, for a while.
    const away = live && seat.reconnect_in != null;
    const out = !away && ['folded', 'sitting_out', 'disconnected', 'busted', 'left'].includes(seat.status);
    const holding = live && (seat.status === 'active' || seat.status === 'allin' || away);

    let cards = [];
    if (seat.cards) cards = seat.cards.map((code) => cardEl(code, 'small'));
    else if (holding) cards = [cardEl('back', 'small'), cardEl('back', 'small')];

    let status = '', statusClass = 'status', awayUntil = null;
    if (away) {
      awayUntil = Date.now() + seat.reconnect_in * 1000;
      status = t('status.away', { n: seat.reconnect_in });
      statusClass += ' away';
    }
    else if (out || seat.status === 'allin') {
      status = t('status.' + seat.status);
      if (seat.status === 'allin') statusClass += ' allin';
    }
    else if (seat.hand) {
      status = handName(S.lang, seat.hand);
      statusClass += ' made';
    }

    const badges = [];
    if (st.host === seat.seat) {
      badges.push(el('span', { class: 'badge host', title: t('badge.host.label') }, t('badge.host')));
    }
    if (st.hand > 0 && st.dealer === seat.seat) {
      badges.push(el('span', { class: 'badge', title: t('badge.dealer.label') }, t('badge.dealer')));
    }
    if (st.small_blind === seat.seat) {
      badges.push(el('span', { class: 'badge blind', title: t('badge.small_blind.label') }, t('badge.small_blind')));
    }
    if (st.big_blind === seat.seat) {
      badges.push(el('span', { class: 'badge blind', title: t('badge.big_blind.label') }, t('badge.big_blind')));
    }

    const turn = st.turn === seat.seat;
    const classes = ['seat', mine && 'me', turn && 'turn', winners.includes(seat.seat) && 'winner', out && 'out'];
    // The host may send anyone else away: a bot at once, a person after asking.
    const kick = () => send({ type: 'kick', seat: seat.seat });
    const remove = st.host === st.you && !mine && seat.status !== 'left'
      ? el('button', { class: 'remove', type: 'button', title: t('kick.label', { name: seat.name }),
                       'aria-label': t('kick.label', { name: seat.name }),
                       onclick: () => (seat.bot ? kick() : confirmThen(t('confirm.kick', { name: seat.name }), t('kick.label', { name: seat.name }), kick)) },
           '×')
      : null;
    // The cards stand above a name plate: picture, name, chips, what is happening.
    nodes.push(el('div', { class: classes.filter(Boolean).join(' '), 'data-seat': seat.seat, style: `left:${at.x}%;top:${at.y}%` },
      el('div', { class: 'cards' }, cards),
      el('div', { class: 'plate' },
        remove,
        el('div', { class: 'badges' }, badges),
        avatarEl(seat),
        el('div', { class: 'who' },
          el('div', { class: 'name', title: seat.name }, mine ? `${seat.name} (${t('you')})` : seat.name),
          el('div', { class: 'stack' }, formatChips(seat.stack), seat.bot && el('span', { class: 'tag' }, t('bot.tag'))),
          // On a narrow screen the bet is written in the seat; on a wide one it lies on the felt.
          seat.bet > 0 && el('div', { class: 'betline' }, t('bet.label') + ' ' + formatChips(seat.bet)),
          turn && S.turnEndsAt && !away ? el('div', { class: 'timer' })
            : el('div', { class: statusClass, 'data-away-until': awayUntil }, status)))));

    if (seat.bet > 0) {
      nodes.push(el('div', { class: 'bet', 'data-seat': seat.seat }, formatChips(seat.bet)));
    }
  }
  fill($('seats'), nodes);
  placeBets();
}

// Put each bet on the felt just inside its seat. Needs the seats' real sizes,
// so it runs after they are on the page, and again when the window changes.
function placeBets() {
  const ring = $('seats');
  const middle = { x: ring.clientWidth / 2, y: ring.clientHeight / 2 };
  for (const chips of ring.querySelectorAll('.bet')) {
    const seat = ring.querySelector(`.seat[data-seat="${chips.dataset.seat}"]`);
    if (!seat) continue;
    const at = chipPlace({ x: seat.offsetLeft, y: seat.offsetTop }, middle,
                         { width: seat.offsetWidth, height: seat.offsetHeight }, 20);
    chips.style.left = at.x + 'px';
    chips.style.top = at.y + 'px';
  }
}

// ---------------------------------------------------------------- motion
// Cards are dealt from the middle of the table, bets slide out of the seats
// and into the pot, the pot goes to whoever won it, new cards turn over.
// Every state redraws the table, so this compares the state before with the
// one now and moves the new elements from where things were. With reduced
// motion asked for, nothing moves.

const EASE = 'cubic-bezier(0.2, 0.8, 0.2, 1)';
const DEAL_GAP = 45;     // ms between one card and the next
const DEAL_TIME = 320;   // ms for a card to reach its seat
const stillPlease = () => window.matchMedia('(prefers-reduced-motion: reduce)').matches;

function middleOf(node) {
  const r = node.getBoundingClientRect();
  return { x: r.left + r.width / 2, y: r.top + r.height / 2, width: r.width };
}

const seatNode = (seat) => document.querySelector(`#seats .seat[data-seat="${seat}"]`);

// Where the chips lie before a redraw: each bet on the felt (none on a narrow
// screen, where bets are written in the seat), and the pot.
function chipsBefore() {
  const bets = new Map();
  for (const chips of document.querySelectorAll('#seats .bet')) {
    const at = middleOf(chips);
    if (at.width > 0) bets.set(Number(chips.dataset.seat), at);
  }
  const pot = document.querySelector('#center .pot');
  return { bets, pot: pot ? middleOf(pot) : null };
}

// A pile of chips that flies over the page from one point to another, then goes.
function flyChips(from, to, words, delay = 0) {
  const ghost = el('div', { class: 'bet flying', style: `left:${from.x}px;top:${from.y}px` }, words);
  document.body.append(ghost);
  const move = ghost.animate([
    { transform: 'translate(-50%, -50%)', opacity: 1 },
    { opacity: 1, offset: 0.75 },
    { transform: `translate(calc(-50% + ${to.x - from.x}px), calc(-50% + ${to.y - from.y}px)) scale(0.8)`, opacity: 0 },
  ], { duration: 480, delay, easing: EASE, fill: 'both' });
  move.onfinish = () => ghost.remove();
  move.oncancel = () => ghost.remove();
}

// Cards turning face up, one after another.
function flipIn(cards, delay = 0) {
  cards.forEach((card, i) => card.animate([
    { transform: 'perspective(500px) rotateY(-90deg) scale(0.92)', opacity: 0.3 },
    { transform: 'none', opacity: 1 },
  ], { duration: 340, delay: delay + i * 90, easing: EASE, fill: 'backwards' }));
}

// The cards of a new hand, one at a time from the middle, starting left of
// the dealer and going round twice. Run again after every redraw while the
// deal lasts, picking up where it was.
function animateDeal(st, startedAt) {
  const elapsed = performance.now() - startedAt;
  const n = st.seats.length;
  const holders = [];
  for (let k = 1; k <= n; k++) {
    const seat = (st.dealer + k) % n;
    const node = seatNode(seat);
    if (node && node.querySelector('.cards .card')) holders.push(node);
  }
  const total = (2 * holders.length) * DEAL_GAP + DEAL_TIME + 400;
  if (elapsed > total) {
    S.dealing = null;
    return;
  }
  const from = middleOf($('center'));
  holders.forEach((node, i) => {
    node.querySelectorAll('.cards .card').forEach((card, round) => {
      const at = middleOf(card);
      const deal = card.animate([
        { transform: `translate(${from.x - at.x}px, ${from.y - at.y}px) scale(0.6) rotate(-10deg)`, opacity: 0 },
        { opacity: 1, offset: 0.2 },
        { transform: 'none', opacity: 1 },
      ], { duration: DEAL_TIME, delay: (round * holders.length + i) * DEAL_GAP, easing: EASE, fill: 'backwards' });
      deal.currentTime = elapsed;
    });
  });
  // Your own cards, big in the dock, turn over once both have reached your seat.
  const mine = holders.findIndex((node) => Number(node.dataset.seat) === st.you);
  if (mine >= 0) {
    document.querySelectorAll('#mine .card').forEach((card, round) => {
      const turn = card.animate([
        { transform: 'perspective(500px) rotateY(-90deg)', opacity: 0 },
        { transform: 'none', opacity: 1 },
      ], { duration: 340, delay: (holders.length + mine) * DEAL_GAP + DEAL_TIME + round * 90, easing: EASE, fill: 'backwards' });
      turn.currentTime = elapsed;
    });
  }
}

function animateChange(prev, st, before) {
  if (!prev || stillPlease() || document.hidden) return;
  if ((prev.table && prev.table.id) !== (st.table && st.table.id)) return;
  const live = st.stage !== 'waiting';
  const sameHand = prev.hand === st.hand;
  const sameRound = sameHand && prev.stage === st.stage;

  if (!sameHand && live) {
    S.dealing = { hand: st.hand, at: performance.now() };   // drawn by renderGame
  }

  // Bets put in since the last state slide out of their seat.
  for (const chips of document.querySelectorAll('#seats .bet')) {
    const seat = Number(chips.dataset.seat);
    const was = sameRound && prev.seats[seat] ? prev.seats[seat].bet : 0;
    const node = seatNode(seat);
    if (!node || st.seats[seat].bet <= was) continue;
    const from = middleOf(node.querySelector('.plate') || node), at = middleOf(chips);
    if (at.width === 0) continue;
    chips.animate([
      { transform: `translate(calc(-50% + ${from.x - at.x}px), calc(-50% + ${from.y - at.y}px)) scale(0.6)`, opacity: 0 },
      { transform: 'translate(-50%, -50%)', opacity: 1 },
    ], { duration: 300, easing: EASE });
  }

  // The round is over: what lay in front of the seats goes into the pot.
  let gathered = false;
  if (sameHand && !sameRound) {
    const pot = document.querySelector('#center .pot');
    const to = pot ? middleOf(pot) : before.pot || middleOf($('center'));
    for (const [seat, at] of before.bets) {
      const bet = prev.seats[seat] ? prev.seats[seat].bet : 0;
      if (bet > 0) {
        flyChips(at, to, formatChips(bet));
        gathered = true;
      }
    }
  }

  // New cards on the board turn over.
  if (sameHand && st.board.length > prev.board.length) {
    const board = [...document.querySelectorAll('#center .board .card')];
    flipIn(board.slice(prev.board.length, st.board.length), gathered ? 250 : 0);
  }

  // The hand is over: cards shown at the showdown turn over, and the pot
  // goes to whoever won it.
  if (sameHand && prev.stage !== 'waiting' && !live) {
    for (const seat of st.seats) {
      const had = prev.seats[seat.seat];
      const node = seatNode(seat.seat);
      if (seat.cards && had && !had.cards && node) flipIn([...node.querySelectorAll('.cards .card')]);
    }
    const from = before.pot || middleOf($('center'));
    for (const r of st.result || []) {
      const node = seatNode(r.seat);
      if (node) flyChips(from, middleOf(node.querySelector('.plate') || node), formatChips(r.amount), gathered ? 420 : 150);
    }
  }
}

// A vote to end or restart: what is asked, who still has to answer, and for
// a player who has not answered yet, the two answers.
function renderVote(st) {
  const box = $('vote');
  const vote = st.vote;
  box.hidden = !vote;
  if (!vote) { fill(box); return; }
  const me = st.seats[st.you];
  const mine = me && vote.waiting.includes(me.name);
  const answer = (agree) => send({ type: 'vote', agree });
  fill(box,
    el('span', { class: 'what' }, t('vote.' + vote.kind, { name: vote.by })),
    el('span', { class: 'who' }, vote.waiting.length > 0 ? t('vote.waiting', { names: vote.waiting.join(', ') }) : ''),
    el('span', { class: 'left', 'data-vote-countdown': true }),
    mine ? el('div', { class: 'buttons' },
      el('button', { type: 'button', onclick: () => answer(false) }, t('vote.decline')),
      el('button', { class: 'primary', type: 'button', onclick: () => answer(true) }, t('vote.agree'))) : null);
}

function renderCenter(st) {
  const live = st.stage !== 'waiting';
  const nodes = [st.game_over ? el('div', { class: 'over' }, t('over.title'))
                              : el('div', { class: 'stage' }, t('stage.' + st.stage))];
  if (live || st.board.length > 0) {
    const slots = [0, 1, 2, 3, 4].map((i) => cardEl(st.board[i] || 'slot'));
    nodes.push(el('div', { class: 'board' }, slots));
  }
  if (live) {
    nodes.push(el('div', { class: 'pot' }, t('pot') + ' ', el('b', {}, formatChips(st.pot))));
  }
  else if (st.result && st.result.length > 0) {
    nodes.push(el('div', { class: 'result' }, st.result.map((r) => el('div', {},
      t(r.hand ? 'result.win.hand' : 'result.win', { name: r.name, n: formatChips(r.amount), hand: handName(S.lang, r.hand) })))));
  }
  fill($('center'), nodes);
}

function renderMine(st) {
  const me = st.seats[st.you];
  const cards = me && me.cards ? me.cards : ['slot', 'slot'];
  fill($('mine'),
    el('div', { class: 'cards' }, cards.map((code) => cardEl(code, 'large'))),
    st.your_hand && me && me.cards
      ? el('div', { class: 'made' }, t('your.hand', { hand: handName(S.lang, st.your_hand) })) : null);
}

function renderActions(st) {
  const me = st.seats[st.you];
  const box = $('actions');

  if (st.game_over) {
    fill(box, el('div', { class: 'status-line' }, t('over.line')), el('div', { class: 'buttons' },
      el('button', { class: 'primary', type: 'button', onclick: () => send({ type: 'new_game' }) }, t('manage.new')),
      el('button', { type: 'button', onclick: showStandings }, t('over.show'))));
    return;
  }

  // Out of chips: buy in again (it counts against you in the results).
  const busted = me && me.stack === 0 && !(st.stage !== 'waiting' && ['active', 'allin'].includes(me.status));
  const rebuy = busted
    ? el('button', { class: 'primary', type: 'button', onclick: () => send({ type: 'rebuy' }) },
         t('rebuy', { n: formatChips(st.starting_chips || 0) }))
    : null;

  if (st.stage === 'waiting') {
    const ready = st.seats.filter((seat) => seat.status === 'ready').length;
    // Only the host deals and adds bots (anyone, when there is no host).
    const leads = st.host == null || st.host < 0 || st.host === st.you;
    const host = st.seats[st.host];
    const line = st.next_hand_in != null ? el('div', { class: 'status-line', 'data-countdown': true })
               : busted ? el('div', { class: 'status-line' }, t('start.busted'))
               : ready < 2 ? el('div', { class: 'status-line' }, t('start.need'))
               : !leads && host ? el('div', { class: 'status-line' }, t('start.host', { name: host.name }))
               : null;
    fill(box, line, el('div', { class: 'buttons' },
      rebuy,
      leads ? el('button', { class: rebuy ? '' : 'primary', type: 'button', disabled: ready < 2, onclick: () => send({ type: 'start' }) },
                 t(st.hand > 0 ? 'start.next' : 'start')) : null,
      // Alone at the table, adding a bot is the next step: that button leads then.
      leads ? el('button', { class: ready < 2 && !rebuy ? 'primary' : '', type: 'button', onclick: showAddBots }, t('bots.add')) : null));
    return;
  }

  if (!st.actions) {
    const who = st.seats[st.turn];
    const words = me && me.status === 'sitting_out' ? t('turn.watching')
                : who ? t('turn.other', { name: who.name }) : t('turn.none');
    fill(box, el('div', { class: 'status-line' }, words), rebuy ? el('div', { class: 'buttons' }, rebuy) : null);
    return;
  }

  // The viewer's turn.
  const a = st.actions;
  const sizes = raiseSizes(st);
  const act = (action, amount) => send(amount == null ? { type: 'action', action } : { type: 'action', action, amount });
  const buttons = [];
  if (!a.check) {
    buttons.push(el('button', { class: 'fold', type: 'button', 'data-key': 'f', onclick: () => act('fold') }, t('action.fold')));
  }
  buttons.push(a.check
    ? el('button', { type: 'button', 'data-key': 'c', onclick: () => act('check') }, t('action.check'))
    : el('button', { type: 'button', 'data-key': 'c', onclick: () => act('call') },
         t(a.call >= me.stack ? 'action.call.allin' : 'action.call', { n: formatChips(a.call) })));

  let raiseRow = null;
  if (sizes) {
    S.raiseTo = clampRaise(S.raiseTo, sizes);
    const raiseWords = () => t(S.raiseTo >= sizes.max ? 'action.allin' : st.current_bet === 0 ? 'action.bet' : 'action.raise',
                               { n: formatChips(S.raiseTo) });
    const go = el('button', { class: 'primary', type: 'button', onclick: () => act('raise', S.raiseTo) }, raiseWords());
    const slider = el('input', { type: 'range', min: sizes.min, max: sizes.max, step: 1, value: S.raiseTo,
                                 'aria-label': t(st.current_bet === 0 ? 'bet.label' : 'raise.label') });
    const number = el('input', { type: 'number', min: sizes.min, max: sizes.max, step: 1, value: S.raiseTo,
                                 'data-key': 'r', 'aria-label': t(st.current_bet === 0 ? 'bet.label' : 'raise.label') });
    const presets = [];
    const set = (value, from) => {
      S.raiseTo = clampRaise(value, sizes);
      if (from !== slider) slider.value = S.raiseTo;
      if (from !== number) number.value = S.raiseTo;
      go.textContent = raiseWords();
      const share = sizes.max > sizes.min ? (S.raiseTo - sizes.min) / (sizes.max - sizes.min) : 1;
      slider.style.setProperty('--filled', (share * 100).toFixed(1) + '%');
      const chosen = presets.find((p) => p.size === S.raiseTo);
      for (const p of presets) p.button.classList.toggle('on', p === chosen);
    };
    slider.addEventListener('input', () => set(slider.value, slider));
    // While typing, a half-written number is left alone; it is checked on leaving the box.
    number.addEventListener('input', () => {
      const typed = Number(number.value);
      if (Number.isInteger(typed) && typed >= sizes.min && typed <= sizes.max) set(typed, number);
    });
    number.addEventListener('change', () => set(number.value, null));
    number.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') { set(number.value, null); act('raise', S.raiseTo); }
    });
    const preset = (key, value) => {
      const button = el('button', { class: 'small', type: 'button', onclick: () => set(value, null) }, t(key));
      presets.push({ button, size: clampRaise(value, sizes) });
      return button;
    };
    raiseRow = el('div', { class: 'raise-row' },
      el('span', { class: 'label' }, t(st.current_bet === 0 ? 'bet.label' : 'raise.label')),
      number, slider,
      el('div', { class: 'segmented' },
        preset('preset.min', sizes.min), preset('preset.half', sizes.half),
        preset('preset.pot', sizes.pot), preset('preset.max', sizes.max)));
    set(S.raiseTo, null);
    buttons.push(go);
  }

  fill(box,
    el('div', { class: 'status-line yours' }, t('turn.yours')),
    el('div', { class: 'buttons' }, buttons),
    raiseRow,
    el('div', { class: 'keys' }, t('keys.hint')));
}

// One line of the log; `only` appends just that event instead of redrawing.
function logLine(event) {
  const kind = event.kind || '';
  const style = kind === 'hand_start' ? 'hand-start' : kind === 'win' ? 'win'
              : ['flop', 'turn', 'river'].includes(kind) ? 'street' : '';
  return el('li', { class: style }, eventNodes(event));
}

function renderLog(only) {
  const log = $('log');
  const atBottom = log.scrollHeight - log.scrollTop - log.clientHeight < 40;
  if (only && log.children.length > 0 && !log.querySelector('.empty')) {
    log.append(logLine(only));
    while (log.children.length > 400) log.firstChild.remove();
  }
  else if (S.log.length === 0) {
    fill(log, el('li', { class: 'empty' }, t('log.empty')));
  }
  else {
    fill(log, S.log.map(logLine));
  }
  if (atBottom || !only) log.scrollTop = log.scrollHeight;
}

function renderSummary() {
  const sum = S.summary;
  if (!sum) return;
  const body = [];
  for (const group of actionsByStreet(sum)) {
    body.push(el('h3', {}, t('stage.' + group.street), inlineCards(boardOn(group.street, sum.board))));
    body.push(el('ul', {}, group.actions.map((action) => el('li', {}, eventNodes(actionAsEvent(action))))));
  }

  body.push(el('h3', {}, t('summary.showdown')));
  if (sum.showdown && sum.showdown.length > 0) {
    body.push(el('ul', {}, sum.showdown.map((shown) => el('li', {},
      eventNodes({ kind: 'show', name: shown.name, cards: shown.cards, hand: shown.hand })))));
  }
  else {
    body.push(el('p', { class: 'hint' }, t('summary.no_showdown')));
  }

  body.push(el('h3', {}, t('summary.result'), el('span', {}, '· ' + t('summary.pot', { n: formatChips(sum.pot) }))));
  // A bet nobody called went back to the bettor and is not part of the pot.
  if (sum.returned && sum.returned.length > 0) {
    body.push(el('ul', {}, sum.returned.map((back) => el('li', {},
      eventNodes({ kind: 'uncalled', name: back.name, amount: back.amount })))));
  }
  const shown = new Map((sum.showdown || []).map((s) => [s.seat, s.cards]));
  body.push(el('table', { class: 'nets' }, el('tbody', {}, (sum.players || []).map((p) => {
    const cards = p.cards || shown.get(p.seat);
    const net = p.net || 0;
    return el('tr', {},
      el('td', {}, p.seat === sum.you ? `${p.name} (${t('you')})` : p.name),
      el('td', { class: 'cards-cell' }, cards ? inlineCards(cards) : ''),
      el('td', { class: net > 0 ? 'up' : net < 0 ? 'down' : '' }, (net > 0 ? '+' : '') + formatChips(net)));
  }))));

  fill($('summary'),
    el('div', { class: 'head' },
      el('h2', {}, t('summary.title', { n: sum.hand })),
      el('button', { class: 'quiet', type: 'button', onclick: () => $('summary').close() }, t('summary.close'))),
    el('div', { class: 'body' }, body));
}

// Who is up and who is down, once the game has been ended.
function renderStandings() {
  const st = S.state;
  if (!st || !st.standings) return;
  const rows = st.standings.map((row) => el('tr', { class: row.left ? 'gone' : null },
    el('td', {}, row.seat === st.you ? `${row.name} (${t('you')})` : row.name,
       row.bot && el('span', { class: 'tag' }, t('bot.tag')),
       row.left && el('span', { class: 'tag' }, t('standings.left'))),
    el('td', { class: 'chips' }, formatChips(row.stack)),
    el('td', { class: row.net > 0 ? 'up' : row.net < 0 ? 'down' : '' }, (row.net > 0 ? '+' : '') + formatChips(row.net))));
  fill($('standings'),
    el('div', { class: 'head' },
      el('h2', {}, t('standings.title')),
      el('button', { class: 'quiet', type: 'button', onclick: () => $('standings').close() }, t('summary.close'))),
    el('div', { class: 'body' },
      el('table', { class: 'nets' },
        el('thead', {}, el('tr', {}, el('th', {}, t('standings.player')),
           el('th', { class: 'chips' }, t('standings.chips')), el('th', {}, t('standings.net')))),
        el('tbody', {}, rows))),
    el('div', { class: 'foot' },
      el('button', { class: 'primary', type: 'button', onclick: () => send({ type: 'new_game' }) }, t('manage.new'))));
}

function showStandings() {
  renderStandings();
  if (!$('standings').open) $('standings').showModal();
}

// Choose how many bots of which kind to seat. The server plays them.
const STYLES = ['rl', 'equity', 'rules', 'calling', 'random'];
let botStyle = 'rl', botCount = 1;

function renderAddBots() {
  const free = Math.max(0, 12 - ((S.state && S.state.seats.length) || 0));
  botCount = Math.max(1, Math.min(botCount, Math.max(1, free)));
  const choices = STYLES.map((style) => el('label', { class: 'style-choice' },
    el('input', { type: 'radio', name: 'bot-style', value: style, checked: style === botStyle,
                  onchange: () => { botStyle = style; } }),
    el('div', {}, el('b', {}, t('style.' + style)), el('span', {}, t('style.' + style + '.about')))));
  const count = el('input', { id: 'bot-count', type: 'number', min: 1, max: Math.max(1, free), step: 1, value: botCount });
  const go = el('button', { class: 'primary', type: 'button', disabled: free === 0 }, t('bots.button', { n: botCount }));
  const readCount = () => {
    botCount = Math.max(1, Math.min(Number(count.value) || 1, Math.max(1, free)));
    go.textContent = t('bots.button', { n: botCount });
  };
  count.addEventListener('input', readCount);
  count.addEventListener('change', () => { readCount(); count.value = botCount; });
  go.addEventListener('click', () => {
    readCount();
    send({ type: 'add_bot', style: botStyle, count: botCount });
    $('bots').close();
  });
  fill($('bots'),
    el('div', { class: 'head' },
      el('h2', {}, t('bots.title')),
      el('button', { class: 'quiet', type: 'button', onclick: () => $('bots').close() }, t('summary.close'))),
    el('div', { class: 'body' },
      el('fieldset', { class: 'styles' }, el('legend', {}, t('bots.style')), choices),
      el('label', { class: 'count-label', for: 'bot-count' }, t('bots.count')),
      el('div', { class: 'count-row' }, count),
      free === 0 ? el('p', { class: 'error' }, t('bots.full')) : null),
    el('div', { class: 'foot' }, go));
}

function showAddBots() {
  renderAddBots();
  $('bots').showModal();
}

// Ask before something that cannot be undone.
function confirmThen(question, yes, then) {
  const box = $('confirm');
  fill(box,
    el('p', {}, question),
    el('div', { class: 'foot' },
      el('button', { type: 'button', onclick: () => box.close() }, t('confirm.cancel')),
      el('button', { class: 'primary', type: 'button', onclick: () => { box.close(); then(); } }, yes)));
  box.showModal();
}

function notice(text) {
  $('notice').textContent = text;
  clearTimeout(S.noticeTimer);
  S.noticeTimer = setTimeout(() => { $('notice').textContent = ''; }, 5000);
}

// Clocks that run between messages from the server.
function tick() {
  const timer = document.querySelector('.seat.turn .timer');
  if (timer && S.turnEndsAt) timer.textContent = formatSeconds((S.turnEndsAt - Date.now()) / 1000);
  for (const away of document.querySelectorAll('[data-away-until]')) {
    away.textContent = t('status.away', { n: Math.max(0, Math.ceil((Number(away.dataset.awayUntil) - Date.now()) / 1000)) });
  }
  const voteLeft = document.querySelector('[data-vote-countdown]');
  if (voteLeft && S.voteEndsAt) {
    voteLeft.textContent = t('vote.seconds', { n: Math.max(0, Math.ceil((S.voteEndsAt - Date.now()) / 1000)) });
  }
  const countdown = document.querySelector('[data-countdown]');
  if (countdown && S.nextHandAt) {
    countdown.textContent = t('start.countdown', { n: Math.max(0, Math.ceil((S.nextHandAt - Date.now()) / 1000)) });
  }
}

// ---------------------------------------------------------------- the player's input

$('create-open').addEventListener('click', showCreate);

$('lang').addEventListener('click', () => {
  S.lang = S.lang === 'zh' ? 'en' : 'zh';
  remember(localStorage, 'th.lang', S.lang);
  renderAll();
});

$('summary-button').addEventListener('click', () => {
  renderSummary();
  $('summary').showModal();
});

// With other people at the table these only ask: everyone has to agree.
const together = () => Boolean(S.state && !S.state.solo);

$('end-game').addEventListener('click', () => {
  confirmThen(t(together() ? 'confirm.end.vote' : 'confirm.end'), t('manage.end'), () => send({ type: 'end_game' }));
});

// Between hands of a finished game nothing is lost by starting over: no need to ask.
$('new-game').addEventListener('click', () => {
  if (S.state && S.state.game_over) send({ type: 'new_game' });
  else confirmThen(t(together() ? 'confirm.new.vote' : 'confirm.new'), t('manage.new'), () => send({ type: 'new_game' }));
});

$('leave').addEventListener('click', () => {
  confirmThen(t('confirm.leave'), t('manage.leave'), () => send({ type: 'leave' }));
});

// Clicking the dimmed page around a dialog closes it.
for (const dialog of document.querySelectorAll('dialog')) {
  dialog.addEventListener('click', (e) => {
    if (e.target === dialog) dialog.close();
  });
}

document.addEventListener('keydown', (e) => {
  if (e.ctrlKey || e.metaKey || e.altKey || document.querySelector('dialog[open]') || $('game').hidden) return;
  if (e.target instanceof HTMLInputElement && e.target.type !== 'range') return;
  if (!/^[a-z]$/i.test(e.key)) return;
  const target = document.querySelector(`#actions [data-key="${e.key.toLowerCase()}"]`);
  if (!target) return;
  e.preventDefault();
  if (target instanceof HTMLInputElement) { target.focus(); target.select(); }
  else target.click();
});

window.addEventListener('resize', placeBets);

$('name').value = remembered(localStorage, 'th.name') || '';
renderAll();
setInterval(tick, 250);
connect();
