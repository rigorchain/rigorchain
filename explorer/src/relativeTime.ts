/**
 * Relative labels for block header times.
 * Past wording matches the server-rendered explorer. A header later than the
 * reference clock is never folded into "just now".
 */

function phrase(n: number, unit: string, future: boolean): string {
  const word = n === 1 ? unit : `${unit}s`;
  return future ? `in ${n} ${word}` : `${n} ${word} ago`;
}

function pastLabel(diff: number): string {
  if (diff < 5) return "just now";
  if (diff < 60) return phrase(diff, "second", false);
  const minutes = Math.floor(diff / 60);
  if (minutes < 60) return phrase(minutes, "minute", false);
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return phrase(hours, "hour", false);
  const days = Math.floor(hours / 24);
  return phrase(days, "day", false);
}

function futureLabel(ahead: number): string {
  if (ahead < 60) return phrase(ahead, "second", true);
  const minutes = Math.floor(ahead / 60);
  if (minutes < 60) return phrase(minutes, "minute", true);
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return phrase(hours, "hour", true);
  const days = Math.floor(hours / 24);
  return phrase(days, "day", true);
}

/** UTC display of a whole-second header time. No extra precision is added. */
export function formatBlockHeaderTimeUtc(unixSeconds: unknown): string {
  const t = Number(unixSeconds);
  if (!Number.isFinite(t) || t <= 0) return "";
  const whole = Math.trunc(t);
  const iso = new Date(whole * 1000).toISOString();
  return `${iso.slice(0, 19).replace("T", " ")} UTC`;
}

export function formatRelativeTime(unixSeconds: unknown, nowUnix?: number): string {
  const t = Number(unixSeconds);
  if (!Number.isFinite(t) || t <= 0) return "—";
  const now =
    nowUnix !== undefined && Number.isFinite(nowUnix) ? nowUnix : Math.floor(Date.now() / 1000);
  const signed = Math.floor(now - t);
  if (!Number.isFinite(signed)) return "—";
  if (signed < 0) return futureLabel(-signed);
  return pastLabel(signed);
}
