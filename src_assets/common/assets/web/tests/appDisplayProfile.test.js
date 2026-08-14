import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import test from 'node:test'

import { normalizeAppDisplayProfile } from '../utils/appDisplayProfile.js'

test('未配置应用显示方案时不写入任何显示字段', () => {
  const normalized = normalizeAppDisplayProfile({
    name: 'Desktop',
    'display-target': '',
    'display-device-prep': 'ensure_only_display',
    'display-obsolete-option': 'legacy',
  })

  assert.deepEqual(normalized, { name: 'Desktop' })
})

test('无操作显示布局能够原样保存', () => {
  const normalized = normalizeAppDisplayProfile({
    name: 'Desktop',
    'display-target': 'virtual',
    'display-device-prep': 'no_operation',
    'display-resolution-mode': 'client',
    'display-refresh-rate-mode': 'client',
  })

  assert.equal(normalized['display-device-prep'], 'no_operation')
  assert.equal(normalized['display-target'], 'virtual')
})

test('原样物理屏只保留目标和指定显示器', () => {
  const normalized = normalizeAppDisplayProfile({
    name: 'Desktop',
    'display-target': 'physical-current',
    'display-output-name': '\\\\.\\DISPLAY2',
    'display-device-prep': 'ensure_only_display',
    'display-resolution-mode': 'fixed',
    'display-resolution': '1920x1080',
    'display-refresh-rate-mode': 'fixed',
    'display-refresh-rate': '60',
    'display-disconnect-action': 'restore',
  })

  assert.deepEqual(normalized, {
    name: 'Desktop',
    'display-target': 'physical-current',
    'display-output-name': '\\\\.\\DISPLAY2',
  })
})

test('应用编辑器直接复用主设置显示准备组件', async () => {
  const source = await readFile(new URL('../components/AppEditor.vue', import.meta.url), 'utf8')

  assert.match(source, /import DisplayPreparationPicker from/)
  assert.match(source, /<DisplayPreparationPicker/)
  assert.match(source, /provide\(\s*'platform'/)
  assert.doesNotMatch(source, /id="displayDevicePrep"/)
})
