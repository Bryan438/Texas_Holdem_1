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
  joinError: null,
  state: null,
  log: remembered(sessionStorage, 'th.name') ? recalled('th.log', []) : [],
  summary: remembered(sessionStorage, 'th.name') ? recalled('th.summary', null) : null,
  wasOver: false,
  raiseTo: null,
  decision: '',
  turnEndsAt: 0,
  nextHandAt: 0,
  noticeTimer: 0,
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
    if (S.wantName) sendJoin(S.wantName);
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

const tokenKey = (name) => `th.token.${location.host}.${name}`;

function sendJoin(name) {
  const message = { type: 'join', name };
  const token = remembered(localStorage, tokenKey(name));
  if (token) message.token = token;
  send(message);
}

function onMessage(m) {
  if (m.type === 'welcome') {
    S.joined = true;
    S.joinError = null;
    S.wantName = m.name;
    remember(localStorage, tokenKey(m.name), m.token);
    remember(localStorage, 'th.name', m.name);
    remember(sessionStorage, 'th.name', m.name);   // a reload of this tab sits down again
    renderScreen();
  }
  else if (m.type === 'state') {
    S.state = m;
    S.turnEndsAt = m.turn_time_left == null ? 0 : Date.now() + m.turn_time_left * 1000;
    S.nextHandAt = m.next_hand_in == null ? 0 : Date.now() + m.next_hand_in * 1000;
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
  else if (m.type === 'error') {
    if (!S.joined) {
      S.joinError = m;
      S.wantName = '';
      try { sessionStorage.removeItem('th.name'); } catch { /* nothing to forget */ }
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
  $('join-title').textContent = t('join.title');
  $('join-label').textContent = t('join.name');
  $('join-button').textContent = t('join.button');
  $('join-hint').textContent = t('join.hint');
  $('log-title').textContent = t('log.title');
  $('summary-button').textContent = t('summary.button');
  $('summary-button').hidden = !S.summary;
  $('end-game').textContent = t('manage.end');
  $('new-game').textContent = t('manage.new');
  renderConnection();
}

function renderScreen() {
  $('join').hidden = S.joined;
  $('game').hidden = !S.joined;
  $('join-error').textContent = S.joinError ? errorText(S.lang, S.joinError) : '';
  if (S.joined) renderGame();
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
  // Ending or restarting is offered only while every other player is a bot.
  $('manage').hidden = !st.solo;
  $('end-game').hidden = Boolean(st.game_over);
  $('meta').textContent = (st.hand > 0 ? t('meta.hand', { n: st.hand }) + ' · ' : '') +
                          t('meta.blinds', { sb: st.blinds[0], bb: st.blinds[1] });
  renderSeats(st);
  renderCenter(st);
  renderMine(st);
  renderActions(st);
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
    const out = !away && ['folded', 'sitting_out', 'disconnected', 'busted'].includes(seat.status);
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
    // A bot added from a page may be sent away between hands.
    const remove = seat.house && !live
      ? el('button', { class: 'remove', type: 'button', title: t('bots.remove', { name: seat.name }),
                       'aria-label': t('bots.remove', { name: seat.name }),
                       onclick: () => send({ type: 'remove_bot', seat: seat.seat }) }, '×')
      : null;
    nodes.push(el('div', { class: classes.filter(Boolean).join(' '), 'data-seat': seat.seat, style: `left:${at.x}%;top:${at.y}%` },
      remove,
      el('div', { class: 'badges' }, badges),
      el('div', { class: 'cards' }, cards),
      el('div', { class: 'name', title: seat.name }, mine ? `${seat.name} (${t('you')})` : seat.name),
      el('div', { class: 'stack' }, formatChips(seat.stack), seat.bot && el('span', { class: 'tag' }, t('bot.tag'))),
      // On a narrow screen the bet is written in the seat; on a wide one it lies on the felt.
      seat.bet > 0 && el('div', { class: 'betline' }, t('bet.label') + ' ' + formatChips(seat.bet)),
      turn && S.turnEndsAt && !away ? el('div', { class: 'timer' })
        : el('div', { class: statusClass, 'data-away-until': awayUntil }, status)));

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

  if (st.stage === 'waiting') {
    const ready = st.seats.filter((seat) => seat.status === 'ready').length;
    const line = st.next_hand_in != null ? el('div', { class: 'status-line', 'data-countdown': true })
               : ready < 2 ? el('div', { class: 'status-line' }, t(me && me.status === 'busted' ? 'start.busted' : 'start.need'))
               : null;
    fill(box, line, el('div', { class: 'buttons' },
      el('button', { class: 'primary', type: 'button', disabled: ready < 2, onclick: () => send({ type: 'start' }) },
         t(st.hand > 0 ? 'start.next' : 'start')),
      // Alone at the table, adding a bot is the next step: that button leads then.
      el('button', { class: ready < 2 ? 'primary' : '', type: 'button', onclick: showAddBots }, t('bots.add'))));
    return;
  }

  if (!st.actions) {
    const who = st.seats[st.turn];
    const words = me && me.status === 'sitting_out' ? t('turn.watching')
                : who ? t('turn.other', { name: who.name }) : t('turn.none');
    fill(box, el('div', { class: 'status-line' }, words));
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
    const set = (value, from) => {
      S.raiseTo = clampRaise(value, sizes);
      if (from !== slider) slider.value = S.raiseTo;
      if (from !== number) number.value = S.raiseTo;
      go.textContent = raiseWords();
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
    const preset = (key, value) => el('button', { class: 'small', type: 'button', onclick: () => set(value, null) }, t(key));
    raiseRow = el('div', { class: 'raise-row' },
      el('span', { class: 'label' }, t(st.current_bet === 0 ? 'bet.label' : 'raise.label')),
      number, slider,
      preset('preset.min', sizes.min), preset('preset.half', sizes.half),
      preset('preset.pot', sizes.pot), preset('preset.max', sizes.max));
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
  const rows = st.standings.map((row) => el('tr', {},
    el('td', {}, row.seat === st.you ? `${row.name} (${t('you')})` : row.name,
       row.bot && el('span', { class: 'tag' }, t('bot.tag'))),
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
  const countdown = document.querySelector('[data-countdown]');
  if (countdown && S.nextHandAt) {
    countdown.textContent = t('start.countdown', { n: Math.max(0, Math.ceil((S.nextHandAt - Date.now()) / 1000)) });
  }
}

// ---------------------------------------------------------------- the player's input

$('join-form').addEventListener('submit', (e) => {
  e.preventDefault();
  const name = $('name').value.trim();
  if (!name) return;
  S.joinError = null;
  S.wantName = name;
  // Sitting down by hand is a fresh start: what an earlier visit logged is gone.
  S.log = [];
  S.summary = null;
  remember(sessionStorage, 'th.log', '[]');
  remember(sessionStorage, 'th.summary', 'null');
  renderLog();
  renderStatic();
  $('join-error').textContent = '';
  if (S.online) sendJoin(name);   // otherwise it is sent as soon as the connection opens
});

$('lang').addEventListener('click', () => {
  S.lang = S.lang === 'zh' ? 'en' : 'zh';
  remember(localStorage, 'th.lang', S.lang);
  renderAll();
});

$('summary-button').addEventListener('click', () => {
  renderSummary();
  $('summary').showModal();
});

$('end-game').addEventListener('click', () => {
  confirmThen(t('confirm.end'), t('manage.end'), () => send({ type: 'end_game' }));
});

// Between hands of a finished game nothing is lost by starting over: no need to ask.
$('new-game').addEventListener('click', () => {
  if (S.state && S.state.game_over) send({ type: 'new_game' });
  else confirmThen(t('confirm.new'), t('manage.new'), () => send({ type: 'new_game' }));
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
