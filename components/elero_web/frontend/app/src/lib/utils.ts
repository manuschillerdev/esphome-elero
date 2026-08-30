import { clsx, type ClassValue } from 'clsx'
import { twMerge } from 'tailwind-merge'

export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs))
}

/**
 * Copy text to the clipboard, with a fallback for non-secure origins.
 *
 * `navigator.clipboard` is only defined on secure origins. The gateway is
 * served over plain HTTP on a LAN address, so the async API is absent there and
 * the hidden-textarea path is the only one that works. Returns whether the copy
 * succeeded so callers can tell the user when it did not.
 */
export async function copyText(text: string): Promise<boolean> {
  if (navigator.clipboard?.writeText) {
    try {
      await navigator.clipboard.writeText(text)
      return true
    } catch {
      // Permission denied, or a browser that exposes the API but blocks it.
      // Fall through to the textarea path.
    }
  }
  return copyViaTextarea(text)
}

function copyViaTextarea(text: string): boolean {
  const ta = document.createElement('textarea')
  ta.value = text
  ta.setAttribute('readonly', '')
  // Off-screen but still selectable. `position: fixed` keeps the page from
  // scrolling when the textarea takes focus.
  ta.style.position = 'fixed'
  ta.style.top = '0'
  ta.style.left = '-9999px'
  document.body.appendChild(ta)

  const selection = document.getSelection()
  const previous = selection && selection.rangeCount > 0 ? selection.getRangeAt(0) : null

  ta.select()
  ta.setSelectionRange(0, text.length)

  let copied = false
  try {
    // Deprecated, but the only synchronous copy available on an insecure origin.
    copied = document.execCommand('copy')
  } catch {
    copied = false
  }

  document.body.removeChild(ta)
  if (selection && previous) {
    selection.removeAllRanges()
    selection.addRange(previous)
  }
  return copied
}
