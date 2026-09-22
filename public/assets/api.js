// Small fetch wrapper shared by the booking page and the dashboard.

export class ApiError extends Error {
  constructor(status, code, message, details) {
    super(message);
    this.status = status;
    this.code = code;
    this.details = details || {};
  }

  /** Field errors from a validation_failed response, or an empty object. */
  get fields() {
    return (this.details && this.details.fields) || {};
  }
}

export async function api(method, path, body, csrf) {
  const headers = { Accept: 'application/json' };
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  if (csrf) headers['X-CSRF-Token'] = csrf;
  let response;
  try {
    response = await fetch(path, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
      credentials: 'same-origin',
    });
  } catch (error) {
    throw new ApiError(0, 'network', 'Could not reach the server. Check your connection and try again.');
  }
  const text = await response.text();
  let data = null;
  if (text) {
    try { data = JSON.parse(text); } catch (error) { data = null; }
  }
  if (!response.ok) {
    const error = (data && data.error) || { code: 'http_' + response.status, message: 'Request failed (' + response.status + ').' };
    throw new ApiError(response.status, error.code, error.message, error.details);
  }
  return data;
}

/** Creates an element. Text goes through textContent, never innerHTML. */
export function el(tag, attrs = {}, children = []) {
  const node = document.createElement(tag);
  for (const [name, value] of Object.entries(attrs)) {
    if (value === null || value === undefined || value === false) continue;
    if (name === 'class') node.className = value;
    else if (name === 'text') node.textContent = value;
    else if (name === 'dataset') Object.assign(node.dataset, value);
    else if (name.startsWith('on') && typeof value === 'function') node.addEventListener(name.slice(2), value);
    else if (name === 'value' && 'value' in node) node.value = value;
    else if (name === 'checked' || name === 'disabled' || name === 'selected' || name === 'required') node[name] = Boolean(value);
    else node.setAttribute(name, value === true ? '' : String(value));
  }
  for (const child of Array.isArray(children) ? children : [children]) {
    if (child === null || child === undefined || child === false) continue;
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}

export function clear(node) {
  while (node.firstChild) node.removeChild(node.firstChild);
  return node;
}
