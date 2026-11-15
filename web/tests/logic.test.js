// Run with: node --test web/tests
import test from 'node:test';
import assert from 'node:assert/strict';
import { cardParts, seatPosition, chipPlace, raiseSizes, clampRaise, decisionKey, actionAsEvent, actionsByStreet,
         boardOn, formatChips, formatSeconds } from '../logic.js';
import { LANGUAGES, translate, handName, eventKey, eventPieces, errorText, allKeys } from '../i18n.js';

test('cards are read from their two-letter codes', () => {
  assert.deepEqual(cardParts('Td'), { rank: '10', suit: '♦', red: true });
  assert.deepEqual(cardParts('As'), { rank: 'A', suit: '♠', red: false });
  assert.equal(cardParts('back'), null);
  assert.equal(cardParts('1s'), null);
  assert.equal(cardParts('Ax'), null);
  assert.equal(cardParts(null), null);
});

test('the viewer sits at the bottom and play goes clockwise', () => {
  const me = seatPosition(2, 2, 4);
  assert.ok(Math.abs(me.x - 50) < 1e-9 && me.y > 99);
  const next = seatPosition(3, 2, 4);          // acts after me: on my left
  assert.ok(next.x < 1 && Math.abs(next.y - 50) < 1e-9);
  const across = seatPosition(0, 2, 4);
  assert.ok(Math.abs(across.x - 50) < 1e-9 && across.y < 1);
  const before = seatPosition(1, 2, 4);        // acted before me: on my right
  assert.ok(before.x > 99);
  // Chips sit between the seat and the middle of the table.
  const chips = seatPosition(2, 2, 4, 0.5);
  assert.ok(chips.y > 50 && chips.y < me.y);
  // Every seat of a full table has its own place, inside the table.
  const places = new Set();
  for (let seat = 0; seat < 12; seat++) {
    const p = seatPosition(seat, 5, 12);
    assert.ok(p.x >= -1e-9 && p.x <= 100 + 1e-9 && p.y >= -1e-9 && p.y <= 100 + 1e-9);
    places.add(p.x.toFixed(3) + ',' + p.y.toFixed(3));
  }
  assert.equal(places.size, 12);
});

test('a bet lies just inside its seat, toward the middle', () => {
  const middle = { x: 400, y: 200 }, box = { width: 120, height: 80 };
  // Seat at the bottom: straight up, past the box's top edge.
  assert.deepEqual(chipPlace({ x: 400, y: 400 }, middle, box, 20), { x: 400, y: 340 });
  // Seat on the left: straight right, past its side.
  assert.deepEqual(chipPlace({ x: 0, y: 200 }, middle, box, 20), { x: 80, y: 200 });
  // Seat at a corner: leaves through whichever edge comes first, never inside the box.
  const corner = chipPlace({ x: 100, y: 380 }, middle, box, 20);
  assert.ok(corner.x > 100 && corner.y < 380);
  assert.ok(Math.abs(corner.x - 100) >= 60 || Math.abs(corner.y - 380) >= 40);
  // A table so small the seat covers the middle: stop there, do not shoot past.
  const tiny = chipPlace({ x: 400, y: 230 }, middle, box, 0);
  assert.deepEqual(tiny, { x: 400, y: 200 });
  assert.deepEqual(chipPlace(middle, middle, box, 20), middle);
});

test('raise sizes follow the pot and stay inside what the server allows', () => {
  // 20 in the pot, nobody has bet: half pot is 10, pot is 20.
  let state = { pot: 20, current_bet: 0, actions: { check: true, call: 0, raise: { min: 2, max: 190 } } };
  assert.deepEqual(raiseSizes(state), { min: 2, max: 190, half: 10, pot: 20 });
  // Facing a bet of 10 into 20 (pot is now 30): a pot-sized raise calls 10 and adds 40.
  state = { pot: 30, current_bet: 10, actions: { check: false, call: 10, raise: { min: 20, max: 190 } } };
  assert.deepEqual(raiseSizes(state), { min: 20, max: 190, half: 30, pot: 50 });
  // A short stack: everything is capped at all-in.
  state = { pot: 100, current_bet: 0, actions: { check: true, call: 0, raise: { min: 2, max: 30 } } };
  assert.deepEqual(raiseSizes(state), { min: 2, max: 30, half: 30, pot: 30 });
  assert.equal(raiseSizes({ pot: 100, current_bet: 50, actions: { call: 50, raise: null } }), null);
  assert.equal(raiseSizes({ pot: 100 }), null);

  const range = { min: 4, max: 60 };
  assert.equal(clampRaise('12', range), 12);
  assert.equal(clampRaise(1, range), 4);
  assert.equal(clampRaise(999, range), 60);
  assert.equal(clampRaise('abc', range), 4);
  assert.equal(clampRaise(12.6, range), 13);
});

test('a new decision is told apart from the same one sent again', () => {
  const state = { hand: 3, stage: 'flop', current_bet: 10, actions: { call: 10, raise: { min: 20, max: 190 } } };
  const key = decisionKey(state);
  assert.equal(decisionKey({ ...state, pot: 99 }), key);          // someone else's news
  assert.notEqual(decisionKey({ ...state, current_bet: 30 }), key);
  assert.notEqual(decisionKey({ ...state, stage: 'turn' }), key);
  assert.equal(decisionKey({ hand: 3, stage: 'flop' }), '');
});

test('a finished hand is grouped by street', () => {
  const summary = {
    board: ['Ah', 'Kd', '2c', '9s'],
    actions: [
      { street: 'preflop', name: 'A', action: 'small_blind', amount: 1 },
      { street: 'preflop', name: 'B', action: 'big_blind', amount: 2 },
      { street: 'preflop', name: 'A', action: 'call', amount: 1 },
      { street: 'flop', name: 'B', action: 'bet', amount: 4, to: 4 },
      { street: 'flop', name: 'A', action: 'raise', amount: 12, to: 12, allin: true },
      { street: 'turn', name: 'B', action: 'fold', amount: 0, reason: 'timeout' },
    ],
  };
  const groups = actionsByStreet(summary);
  assert.deepEqual(groups.map((g) => [g.street, g.actions.length]), [['preflop', 3], ['flop', 2], ['turn', 1]]);
  assert.deepEqual(boardOn('preflop', summary.board), []);
  assert.deepEqual(boardOn('flop', summary.board), ['Ah', 'Kd', '2c']);
  assert.deepEqual(boardOn('turn', summary.board), ['Ah', 'Kd', '2c', '9s']);
  // A raise is worded by what it was raised TO, as in the live log.
  assert.deepEqual(actionAsEvent(summary.actions[4]), { kind: 'raise', name: 'A', amount: 12, allin: true });
  assert.deepEqual(actionAsEvent(summary.actions[5]), { kind: 'fold', name: 'B', amount: 0, allin: false, reason: 'timeout' });
  assert.deepEqual(actionsByStreet({}), []);
});

test('numbers and clocks', () => {
  assert.equal(formatChips(1234567), '1,234,567');
  assert.equal(formatSeconds(299.2), '5:00');
  assert.equal(formatSeconds(7), '0:07');
  assert.equal(formatSeconds(-3), '0:00');
});

test('both languages have every string, with the same placeholders', () => {
  assert.deepEqual(LANGUAGES, ['en', 'zh']);
  const holes = (s) => (s.match(/\{\w+\}/g) || []).sort().join(' ');
  assert.deepEqual(allKeys('zh').sort(), allKeys('en').sort());
  for (const key of allKeys('en')) {
    assert.equal(holes(translate('zh', key)), holes(translate('en', key)), key);
  }
});

test('events are worded in either language', () => {
  const text = (lang, event) => eventPieces(lang, event).map((p) => (typeof p === 'string' ? p : '[' + p.cards.join(' ') + ']')).join('');
  const raise = { kind: 'raise', name: 'Bob', amount: 20, allin: false, text: 'Bob raises to 20' };
  assert.equal(text('en', raise), 'Bob raises to 20');
  assert.equal(text('zh', raise), 'Bob 加注到 20');
  assert.equal(text('zh', { ...raise, allin: true }), 'Bob 加注到 20（全下）');
  assert.equal(text('en', { kind: 'call', name: 'Al', amount: 8, allin: true }), 'Al calls 8 and is all-in');
  assert.equal(text('zh', { kind: 'flop', cards: ['Qh', '8h', '4s'] }), '翻牌：[Qh 8h 4s]');
  assert.equal(text('en', { kind: 'show', name: 'Al', cards: ['As', 'Ad'], hand: 'One pair' }), 'Al shows [As Ad] — One pair');
  assert.equal(text('zh', { kind: 'show', name: 'Al', cards: ['As', 'Ad'], hand: 'One pair' }), 'Al 亮牌 [As Ad]，一对');
  assert.equal(text('zh', { kind: 'win', name: 'Al', amount: 40, hand: 'Full house' }), 'Al 赢得 40（葫芦）');
  assert.equal(text('zh', { kind: 'win', name: 'Al', amount: 3, hand: '' }), 'Al 赢得 3');
  assert.equal(text('zh', { kind: 'hand_start', hand: 7, name: 'Al' }), '第 7 手，庄家 Al');
  assert.equal(text('zh', { kind: 'joined', name: 'Al', next_hand: true }), 'Al 入座（下一手开始参与）');
  assert.equal(text('zh', { kind: 'fold', name: 'Al', reason: 'disconnected' }), 'Al 弃牌（掉线）');
  assert.equal(text('en', { kind: 'check', name: 'Al', reason: 'timeout' }), 'Al checks (out of time)');
  assert.equal(text('zh', { kind: 'disconnected', name: 'Al', grace: 30 }), 'Al 掉线了，30 秒内回来可以继续这一手');
  assert.equal(text('en', { kind: 'disconnected', name: 'Al' }), 'Al disconnected');
  assert.equal(text('zh', { kind: 'game_over', name: 'Al' }), 'Al 结束了牌局');
  assert.equal(text('en', { kind: 'new_game', name: 'Al', amount: 200 }), 'Al started a new game: everyone has 200 chips');
  assert.equal(text('zh', { kind: 'hand_cancelled', hand: 4 }), '第 4 手作废，下注已退回');
  // A kind this page has never heard of: show the server's own line.
  assert.equal(eventKey({ kind: 'rabbit_hunt' }), null);
  assert.equal(text('zh', { kind: 'rabbit_hunt', text: 'Bob peeks' }), 'Bob peeks');
  assert.equal(text('zh', { text: 'An old server' }), 'An old server');
  // An unknown reason falls back to the plain wording.
  assert.equal(text('en', { kind: 'fold', name: 'Al', reason: 'new_reason' }), 'Al folds');
});

test('hand names and errors', () => {
  assert.equal(handName('zh', 'Straight flush'), '同花顺');
  assert.equal(handName('zh', 'Royal something'), 'Royal something');
  assert.equal(handName('en', ''), '');
  assert.equal(errorText('zh', { code: 'not_your_turn', message: 'it is not your turn' }), '还没轮到你。');
  // No wording for this code: the server's own message is better than nothing.
  assert.equal(errorText('zh', { code: 'bad_action', message: 'raise must be to between 4 and 60' }),
               'raise must be to between 4 and 60');
  assert.equal(errorText('en', { message: 'something else' }), 'something else');
  assert.equal(errorText('zh', { code: 'not_alone', message: 'x' }), '只有桌上除了你都是机器人时才能这样做。');
});
