// Every word the web client shows, in English and Chinese. The server sends
// what happened as fields ("kind", "name", "amount", ...); this file words it.

export const LANGUAGES = ['en', 'zh'];

const STRINGS = {
  en: {
    'title': "Texas Hold'em",
    'lang.switch': '中文',
    'lang.switch.label': 'Switch to Chinese',
    'join.title': 'Take a seat',
    'join.name': 'Your name',
    'join.button': 'Sit down',
    'join.hint': 'Bots and players on the terminal client sit at this same table.',
    'conn.connecting': 'Connecting…',
    'conn.online': 'Connected',
    'conn.offline': 'Connection lost, trying again…',
    'meta.hand': 'Hand #{n}',
    'meta.blinds': 'Blinds {sb}/{bb}',
    'stage.waiting': 'Between hands',
    'stage.preflop': 'Preflop',
    'stage.flop': 'Flop',
    'stage.turn': 'Turn',
    'stage.river': 'River',
    'pot': 'Pot',
    'you': 'You',
    'badge.dealer': 'D',
    'badge.small_blind': 'SB',
    'badge.big_blind': 'BB',
    'badge.dealer.label': 'Dealer',
    'badge.small_blind.label': 'Small blind',
    'badge.big_blind.label': 'Big blind',
    'status.folded': 'Folded',
    'status.allin': 'All-in',
    'status.sitting_out': 'Plays next hand',
    'status.disconnected': 'Disconnected',
    'status.busted': 'Out of chips',
    'manage.end': 'End game',
    'manage.new': 'Restart',
    'confirm.end': 'End the game and see who is up? The hand in progress is cancelled and its bets go back.',
    'confirm.new': 'Start over? Everyone goes back to the starting chips, and the hand in progress is cancelled.',
    'confirm.cancel': 'Keep playing',
    'over.title': 'Game over',
    'over.line': 'This game is over.',
    'over.show': 'Show results',
    'standings.title': 'Results',
    'standings.player': 'Player',
    'standings.chips': 'Chips',
    'standings.net': 'Won or lost',
    'bot.tag': 'bot',
    'status.away': 'Away · {n} s to return',
    'action.fold': 'Fold',
    'action.check': 'Check',
    'action.call': 'Call {n}',
    'action.call.allin': 'Call {n} (all-in)',
    'action.bet': 'Bet {n}',
    'action.raise': 'Raise to {n}',
    'action.allin': 'All-in {n}',
    'raise.label': 'Raise to',
    'bet.label': 'Bet',
    'preset.min': 'Min',
    'preset.half': '½ pot',
    'preset.pot': 'Pot',
    'preset.max': 'All-in',
    'turn.yours': 'Your turn',
    'turn.other': 'Waiting for {name}',
    'turn.none': 'Dealing…',
    'turn.watching': 'You play from the next hand',
    'start': 'Deal a hand',
    'start.next': 'Deal the next hand',
    'start.need': 'Waiting for another player with chips',
    'start.countdown': 'Next hand in {n} s',
    'start.busted': 'You are out of chips',
    'result.win': '{name} wins {n}',
    'result.win.hand': '{name} wins {n} with {hand}',
    'your.hand': 'You have: {hand}',
    'log.title': 'Hand log',
    'log.empty': 'Nothing yet. Deal a hand to begin.',
    'summary.button': 'Review last hand',
    'summary.title': 'Hand #{n}',
    'summary.close': 'Close',
    'summary.board': 'Board',
    'summary.showdown': 'Showdown',
    'summary.result': 'Result',
    'summary.no_showdown': 'No showdown: everyone else folded.',
    'summary.pot': 'Pot {n}',
    'keys.hint': 'Keys: F fold · C check or call · R raise',
    'hand.High card': 'High card',
    'hand.One pair': 'One pair',
    'hand.Two pair': 'Two pair',
    'hand.Three of a kind': 'Three of a kind',
    'hand.Straight': 'Straight',
    'hand.Flush': 'Flush',
    'hand.Full house': 'Full house',
    'hand.Four of a kind': 'Four of a kind',
    'hand.Straight flush': 'Straight flush',
    'event.disconnected': '{name} disconnected',
    'event.disconnected.grace': '{name} disconnected ({grace} s to come back)',
    'event.reconnected': '{name} reconnected',
    'event.game_over': '{name} ended the game',
    'event.new_game': '{name} started a new game: everyone has {amount} chips',
    'event.hand_cancelled': 'Hand #{hand} cancelled, bets returned',
    'event.joined': '{name} joined',
    'event.joined.next': '{name} joined (plays from the next hand)',
    'event.not_enough_players': 'Not enough players with chips. Waiting for someone to join.',
    'event.timeout': '{name} ran out of time',
    'event.hand_start': 'Hand #{hand} — dealer {name}',
    'event.small_blind': '{name} posts small blind {amount}',
    'event.big_blind': '{name} posts big blind {amount}',
    'event.fold': '{name} folds',
    'event.fold.disconnected': '{name} folds (disconnected)',
    'event.fold.timeout': '{name} folds (out of time)',
    'event.check': '{name} checks',
    'event.check.timeout': '{name} checks (out of time)',
    'event.call': '{name} calls {amount}',
    'event.call.allin': '{name} calls {amount} and is all-in',
    'event.bet': '{name} bets {amount}',
    'event.bet.allin': '{name} bets {amount} (all-in)',
    'event.raise': '{name} raises to {amount}',
    'event.raise.allin': '{name} raises to {amount} (all-in)',
    'event.flop': 'Flop: {cards}',
    'event.turn': 'Turn: {cards}',
    'event.river': 'River: {cards}',
    'event.show': '{name} shows {cards} — {hand}',
    'event.win': '{name} wins {amount}',
    'event.win.hand': '{name} wins {amount} with {hand}',
    'event.hand_over': 'Hand over.',
    'error.name_invalid': 'A name needs 1 to 24 characters.',
    'error.name_taken': 'Someone at the table already uses that name.',
    'error.name_reserved': 'That name belongs to a player who disconnected. Only they can take the seat back.',
    'error.table_full': 'The table is full.',
    'error.hand_running': 'A hand is already running.',
    'error.not_enough_players': 'A hand needs at least 2 connected players with chips.',
    'error.no_hand': 'No hand is running.',
    'error.not_your_turn': 'It is not your turn.',
    'error.already_seated': 'You are already seated.',
    'error.join_first': 'Sit down first.',
    'error.not_alone': 'Only possible while you are the only person at the table.',
    'error.game_over': 'This game is over. Start a new one.',
  },
  zh: {
    'title': '德州扑克',
    'lang.switch': 'English',
    'lang.switch.label': '切换到英文',
    'join.title': '入座',
    'join.name': '你的名字',
    'join.button': '坐下',
    'join.hint': '机器人和用终端客户端的玩家也坐在这同一张桌上。',
    'conn.connecting': '正在连接…',
    'conn.online': '已连接',
    'conn.offline': '连接断开，正在重试…',
    'meta.hand': '第 {n} 手',
    'meta.blinds': '盲注 {sb}/{bb}',
    'stage.waiting': '等待开局',
    'stage.preflop': '翻牌前',
    'stage.flop': '翻牌',
    'stage.turn': '转牌',
    'stage.river': '河牌',
    'pot': '底池',
    'you': '你',
    'badge.dealer': '庄',
    'badge.small_blind': '小盲',
    'badge.big_blind': '大盲',
    'badge.dealer.label': '庄家',
    'badge.small_blind.label': '小盲',
    'badge.big_blind.label': '大盲',
    'status.folded': '已弃牌',
    'status.allin': '全下',
    'status.sitting_out': '下一手参与',
    'status.disconnected': '已掉线',
    'status.busted': '没有筹码',
    'manage.end': '结束牌局',
    'manage.new': '重新开始',
    'confirm.end': '结束这一场并查看战绩？正在打的这一手会作废，下注退回。',
    'confirm.new': '重新开始？所有人的筹码恢复到起始数量，正在打的这一手会作废。',
    'confirm.cancel': '继续打',
    'over.title': '牌局结束',
    'over.line': '这一场已经结束。',
    'over.show': '查看战绩',
    'standings.title': '战绩',
    'standings.player': '玩家',
    'standings.chips': '筹码',
    'standings.net': '输赢',
    'bot.tag': '机器人',
    'status.away': '掉线 · {n} 秒内可回来',
    'action.fold': '弃牌',
    'action.check': '过牌',
    'action.call': '跟注 {n}',
    'action.call.allin': '跟注 {n}（全下）',
    'action.bet': '下注 {n}',
    'action.raise': '加注到 {n}',
    'action.allin': '全下 {n}',
    'raise.label': '加注到',
    'bet.label': '下注',
    'preset.min': '最小',
    'preset.half': '半池',
    'preset.pot': '满池',
    'preset.max': '全下',
    'turn.yours': '轮到你了',
    'turn.other': '等待 {name} 行动',
    'turn.none': '正在发牌…',
    'turn.watching': '你从下一手开始参与',
    'start': '发牌',
    'start.next': '发下一手',
    'start.need': '等待另一位有筹码的玩家',
    'start.countdown': '{n} 秒后开始下一手',
    'start.busted': '你没有筹码了',
    'result.win': '{name} 赢得 {n}',
    'result.win.hand': '{name} 赢得 {n}（{hand}）',
    'your.hand': '你的牌型：{hand}',
    'log.title': '牌局记录',
    'log.empty': '还没有记录。发一手牌就开始了。',
    'summary.button': '回顾上一手',
    'summary.title': '第 {n} 手',
    'summary.close': '关闭',
    'summary.board': '公共牌',
    'summary.showdown': '摊牌',
    'summary.result': '结果',
    'summary.no_showdown': '没有摊牌：其他人都弃牌了。',
    'summary.pot': '底池 {n}',
    'keys.hint': '快捷键：F 弃牌 · C 过牌或跟注 · R 加注',
    'hand.High card': '高牌',
    'hand.One pair': '一对',
    'hand.Two pair': '两对',
    'hand.Three of a kind': '三条',
    'hand.Straight': '顺子',
    'hand.Flush': '同花',
    'hand.Full house': '葫芦',
    'hand.Four of a kind': '四条',
    'hand.Straight flush': '同花顺',
    'event.disconnected': '{name} 掉线了',
    'event.disconnected.grace': '{name} 掉线了，{grace} 秒内回来可以继续这一手',
    'event.reconnected': '{name} 重新连上了',
    'event.game_over': '{name} 结束了牌局',
    'event.new_game': '{name} 重新开始了牌局：每人 {amount} 筹码',
    'event.hand_cancelled': '第 {hand} 手作废，下注已退回',
    'event.joined': '{name} 入座',
    'event.joined.next': '{name} 入座（下一手开始参与）',
    'event.not_enough_players': '有筹码的玩家不够，等待有人加入。',
    'event.timeout': '{name} 超时',
    'event.hand_start': '第 {hand} 手，庄家 {name}',
    'event.small_blind': '{name} 下小盲 {amount}',
    'event.big_blind': '{name} 下大盲 {amount}',
    'event.fold': '{name} 弃牌',
    'event.fold.disconnected': '{name} 弃牌（掉线）',
    'event.fold.timeout': '{name} 弃牌（超时）',
    'event.check': '{name} 过牌',
    'event.check.timeout': '{name} 过牌（超时）',
    'event.call': '{name} 跟注 {amount}',
    'event.call.allin': '{name} 跟注 {amount}，全下',
    'event.bet': '{name} 下注 {amount}',
    'event.bet.allin': '{name} 下注 {amount}（全下）',
    'event.raise': '{name} 加注到 {amount}',
    'event.raise.allin': '{name} 加注到 {amount}（全下）',
    'event.flop': '翻牌：{cards}',
    'event.turn': '转牌：{cards}',
    'event.river': '河牌：{cards}',
    'event.show': '{name} 亮牌 {cards}，{hand}',
    'event.win': '{name} 赢得 {amount}',
    'event.win.hand': '{name} 赢得 {amount}（{hand}）',
    'event.hand_over': '这一手结束。',
    'error.name_invalid': '名字需要 1 到 24 个字符。',
    'error.name_taken': '桌上已经有人用这个名字了。',
    'error.name_reserved': '这个名字属于一位掉线的玩家，只有他本人能坐回来。',
    'error.table_full': '牌桌已经坐满了。',
    'error.hand_running': '这一手还没打完。',
    'error.not_enough_players': '至少要有 2 位在线且有筹码的玩家才能发牌。',
    'error.no_hand': '现在没有牌局在进行。',
    'error.not_your_turn': '还没轮到你。',
    'error.already_seated': '你已经入座了。',
    'error.join_first': '请先入座。',
    'error.not_alone': '只有桌上除了你都是机器人时才能这样做。',
    'error.game_over': '这一场已经结束，请重新开始。',
  },
};

export function hasString(lang, key) {
  return Object.prototype.hasOwnProperty.call(STRINGS[lang] || {}, key);
}

// The string for `key` with {placeholders} filled in. Falls back to English,
// then to the key itself.
export function translate(lang, key, params = {}) {
  const template = hasString(lang, key) ? STRINGS[lang][key] : hasString('en', key) ? STRINGS.en[key] : key;
  return template.replace(/\{(\w+)\}/g, (whole, name) => (name in params ? String(params[name]) : whole));
}

export function handName(lang, hand) {
  if (!hand) return '';
  return hasString(lang, 'hand.' + hand) ? translate(lang, 'hand.' + hand) : hand;
}

// Which string words an event: "event.call.allin", "event.fold.timeout", ...
// Null when this client does not know the kind (a newer server).
export function eventKey(event) {
  if (!event || typeof event.kind !== 'string') return null;
  let key = 'event.' + event.kind;
  if (event.kind === 'joined' && event.next_hand) key += '.next';
  else if (event.kind === 'disconnected' && event.grace) key += '.grace';
  else if (event.kind === 'win' && event.hand) key += '.hand';
  else if ((event.kind === 'fold' || event.kind === 'check') && event.reason) key += '.' + event.reason;
  else if (['call', 'bet', 'raise'].includes(event.kind) && event.allin) key += '.allin';
  if (hasString('en', key)) return key;
  return hasString('en', 'event.' + event.kind) ? 'event.' + event.kind : null;
}

// An event as a list of pieces to show in order: plain strings, and
// { cards: [...] } where cards are to be drawn. Falls back to the server's
// own English line for kinds this client does not know.
export function eventPieces(lang, event) {
  const key = eventKey(event);
  if (!key) return [String((event && event.text) || '')];
  // "hand" is a hand's number in some events and the name of a made hand in others.
  const text = translate(lang, key, {
    ...event,
    cards: '{cards}',   // left in place: the cards are drawn, not written
    hand: typeof event.hand === 'number' ? event.hand : handName(lang, event.hand),
  });
  const pieces = [];
  for (const part of text.split('{cards}')) {
    if (pieces.length > 0) pieces.push({ cards: event.cards || [] });
    if (part) pieces.push(part);
  }
  return pieces;
}

// An error for the player: worded here when the code is known, otherwise the
// server's own message.
export function errorText(lang, error) {
  const key = 'error.' + ((error && error.code) || '');
  return hasString('en', key) ? translate(lang, key) : String((error && error.message) || '');
}

export function allKeys(lang) {
  return Object.keys(STRINGS[lang] || {});
}
