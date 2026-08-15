const DISPLAY_PROFILE_FIELDS = new Set([
  'display-target',
  'display-device-prep',
  'display-resolution-mode',
  'display-resolution',
  'display-refresh-rate-mode',
  'display-refresh-rate',
  'display-dynamic-resolution-follow-display',
  'display-output-name',
  'display-disconnect-action',
])

export function normalizeAppDisplayProfile(app) {
  const normalized = { ...app }

  for (const key of Object.keys(normalized)) {
    if (key.startsWith('display-') && !DISPLAY_PROFILE_FIELDS.has(key)) delete normalized[key]
  }

  if (!normalized['display-target']) {
    for (const key of DISPLAY_PROFILE_FIELDS) delete normalized[key]
    return normalized
  }

  if (!['', 'no_operation', 'client', 'fixed'].includes(normalized['display-resolution-mode'])) {
    normalized['display-resolution-mode'] = ''
  }
  if (!['', 'no_operation', 'client', 'fixed'].includes(normalized['display-refresh-rate-mode'])) {
    normalized['display-refresh-rate-mode'] = ''
  }
  if (normalized['display-resolution-mode'] !== 'fixed') delete normalized['display-resolution']
  if (normalized['display-refresh-rate-mode'] !== 'fixed') delete normalized['display-refresh-rate']
  if (!['enabled', 'disabled'].includes(normalized['display-dynamic-resolution-follow-display'])) {
    delete normalized['display-dynamic-resolution-follow-display']
  }
  if (normalized['display-target'] !== 'physical') delete normalized['display-output-name']
  return normalized
}
