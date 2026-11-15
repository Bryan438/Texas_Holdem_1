// The parts of the web client that only compute: no page, no network, so they
// can be tested with `node --test web/tests`.

export const SUITS = { s: '♠', h: '♥', d: '♦', c: '♣' };

// "Td" -> { rank: "10", suit: "♦", red: true }; null for anything else.
export function cardParts(code) {
  if (typeof code !== 'string' || code.length !== 2) return null;
  const rank = code[0], suit = code[1];
  if (!SUITS[suit] || !'23456789TJQKA'.includes(rank)) return null;
  return { rank: rank === 'T' ? '10' : rank, suit: SUITS[suit], red: suit === 'h' || suit === 'd' };
}

// Where a seat sits around the table, in percent of the width and height of
// the ring the seats stand on. The viewer is at the bottom centre; the next seat to act is on
// their left, so play goes clockwise as at a real table. `reach` is how far
// from the centre (1 = on the seats' ring).
export function seatPosition(seat, you, count, reach = 1) {
  const n = Math.max(1, count);
  const steps = (((seat - Math.max(0, you)) % n) + n) % n;
  const angle = Math.PI / 2 + (steps * 2 * Math.PI) / n;
  return {
    x: 50 + 50 * reach * Math.cos(angle),
    y: 50 + 50 * reach * Math.sin(angle),
  };
}

// Where a seat's bet lies on the felt: just outside the seat's box, on the
// line from the seat to the middle of the table. All in pixels.
//   seat, middle   centres: { x, y }
//   box            the seat's size: { width, height }
//   gap            from the box's edge to the centre of the chips
export function chipPlace(seat, middle, box, gap) {
  let dx = middle.x - seat.x, dy = middle.y - seat.y;
  const length = Math.hypot(dx, dy);
  if (length < 1e-6) return { x: seat.x, y: seat.y };
  dx /= length;
  dy /= length;
  // How far along that line the box's edge is: whichever side it leaves by.
  const toSide = Math.abs(dx) > 1e-6 ? box.width / 2 / Math.abs(dx) : Infinity;
  const toTopOrBottom = Math.abs(dy) > 1e-6 ? box.height / 2 / Math.abs(dy) : Infinity;
  const out = Math.min(toSide, toTopOrBottom, length) + gap;
  return { x: seat.x + dx * out, y: seat.y + dy * out };
}

// The raise sizes offered on the viewer's turn, as "raise to" totals for this
// round. Null when raising is not allowed.
//   half: call, then add half of what the pot is after calling
//   pot:  call, then add all of it
export function raiseSizes(state) {
  const range = state && state.actions && state.actions.raise;
  if (!range) return null;
  const clamp = (to) => Math.max(range.min, Math.min(range.max, Math.round(to)));
  const afterCall = state.pot + (state.actions.call || 0);
  return {
    min: range.min,
    max: range.max,
    half: clamp(state.current_bet + afterCall / 2),
    pot: clamp(state.current_bet + afterCall),
  };
}

export function clampRaise(value, range) {
  const n = Math.round(Number(value));
  if (!Number.isFinite(n)) return range.min;
  return Math.max(range.min, Math.min(range.max, n));
}

// Changes whenever the viewer faces a new decision, so the raise slider is
// reset then and left alone otherwise.
export function decisionKey(state) {
  if (!state || !state.actions) return '';
  const raise = state.actions.raise;
  return [state.hand, state.stage, state.current_bet, state.actions.call,
          raise ? raise.min : '-', raise ? raise.max : '-'].join('|');
}

// A finished hand's action (from "hand_summary") in the shape of the live
// "event" messages, so both are worded by the same code.
export function actionAsEvent(action) {
  const raised = action.action === 'bet' || action.action === 'raise';
  const event = { kind: action.action, name: action.name, amount: raised ? action.to : action.amount,
                  allin: Boolean(action.allin) };
  if (action.reason) event.reason = action.reason;
  return event;
}

export const STREETS = ['preflop', 'flop', 'turn', 'river'];

// The summary's actions grouped by street, blinds included, empty streets left out.
export function actionsByStreet(summary) {
  const groups = STREETS.map((street) => ({ street, actions: [] }));
  for (const action of (summary && summary.actions) || []) {
    const group = groups.find((g) => g.street === action.street);
    if (group) group.actions.push(action);
  }
  return groups.filter((g) => g.actions.length > 0);
}

// The board as it stood on a street: 0, 3, 4 or 5 cards.
export function boardOn(street, board) {
  const count = { preflop: 0, flop: 3, turn: 4, river: 5 }[street] || 0;
  return (board || []).slice(0, count);
}

export function formatChips(n) {
  return Number(n).toLocaleString('en-US');
}

// "0:07", "4:59"
export function formatSeconds(seconds) {
  const s = Math.max(0, Math.ceil(seconds));
  return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
}
