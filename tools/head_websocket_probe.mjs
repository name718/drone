// Node.js >=22自带WebSocket；电脑上的测试驱动，不部署到ESP或Luckfox。
// stdin逐行接收JSON控制命令，stdout逐行输出ESP返回的JSON遥测。
import readline from 'node:readline';

const socket = new WebSocket(process.argv[2]);
socket.addEventListener('open', () => {
  console.log(JSON.stringify({probe: 'connected'}));
  readline.createInterface({input: process.stdin}).on('line', line => {
    if (socket.readyState === WebSocket.OPEN) socket.send(line);
  }).on('close', () => socket.close());
});
socket.addEventListener('message', event => {
  try { console.log(JSON.stringify(JSON.parse(String(event.data)))); }
  catch { /* 非JSON消息不当作遥测验证结果。 */ }
});
socket.addEventListener('error', () => {
  console.error('ESP WebSocket connection failed');
  process.exitCode = 1;
});
