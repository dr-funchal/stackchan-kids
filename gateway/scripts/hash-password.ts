// Prints the scrypt hash for ADMIN_PASSWORD_HASH. The password comes from stdin so it never lands in shell history:
//   read -rs PW && printf '%s' "$PW" | node scripts/hash-password.ts && unset PW
import { hashPassword } from '../src/web/auth.ts';

let input = '';
for await (const chunk of process.stdin) input += chunk;
const password = input.replace(/\r?\n$/, '');
if (password.length < 8) {
  console.error('password too short');
  process.exit(1);
}
console.log(await hashPassword(password));
