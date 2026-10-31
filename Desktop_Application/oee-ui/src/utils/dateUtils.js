export function getToday() {
  return new Date().toISOString().split("T")[0];
}

export function getYesterday() {
  return new Date(
    Date.now() - 24 * 60 * 60 * 1000
  )
    .toISOString()
    .split("T")[0];
}