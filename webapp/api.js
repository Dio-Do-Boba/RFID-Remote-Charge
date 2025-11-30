/**
 * api.js
 *
 * Responsibilities:
 *   - centralize the interactions with the OneNET HTTP API
 *   - expose helpers for reading and updating device properties
 *
 * Heads-up:
 *   - in production, proxy these requests to keep credentials server-side
 *   - this demo keeps the signature inline for simplicity
 */

/**
 * OneNET client configuration. Move these to environment variables
 * or a backend service for real deployments.
 * USE token.py TO GENERATE YOUR AUTHORIZATION TOKEN.
 */
const ONE_NET_CONFIG = {
  authorization:
    "version=00000000000000000",
  productId: "00000000",
  deviceName: "Test",
  /**
   * Base URL for the query-device-property endpoint (GET).
   * The actual query parameters are appended before each request.
   */
  getInfoUrl:
    "https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=TdTlyD3CtQ&device_name=Test1",
  setInfoUrl: "https://iot-api.heclouds.com/thingmodel/set-device-property",
  /**
   * 轮询间隔（秒）。在 app.js 中会动态展示。
   */
  pollingIntervalSec: 5,
};

/**
 * List of property identifiers we care about.
 * The API returns all properties; this list helps with mapping.
 */
const PROPERTY_KEYS = [
  "Card1",
  "Card2",
  "Card3",
  "C1Charge",
  "C2Charge",
  "C3Charge"
];

/**
 * Transform the raw API payload into the shape expected by the UI.
 *
 * Official response example for query-device-property:
 * [
 *   {
 *     "identifier": "Card1",
 *     "value": "123",  // JSON string
 *     "time": "1592797444539",
 *     ...
 *   },
 *   ...
 * ]
 *
 * @param {any} rawData OneNET JSON response
 * @returns {{ card1: string, card2: string, card3: string, c1Charge: string, updatedAt: string }}
 */
function mapDeviceProperties(rawData) {
  if (!rawData || !rawData.data || !Array.isArray(rawData.data)) {
    console.warn("mapDeviceProperties: data is missing or not an array", rawData);
    return {
      card1: "--",
      card2: "--",
      card3: "--",
      c1Charge: "--",
      c2Charge: "--",
      c3Charge: "--",
      updatedAt: "--",
    };
  }

  // Create a lookup table keyed by identifier for quick access
  const propertyMap = {};
  rawData.data.forEach((item) => {
    if (item.identifier) {
      propertyMap[item.identifier] = item;
    }
  });

  // Debug helper
  console.log("mapDeviceProperties: identifiers found:", Object.keys(propertyMap));

  // Retrieve value for a given identifier. The payload stores values as JSON strings.
  const getValue = (identifier) => {
    const item = propertyMap[identifier];
    if (!item || item.value === undefined || item.value === null) {
      return null;
    }
    // Try parsing in case it was encoded as JSON
    try {
      const parsed = JSON.parse(item.value);
      return parsed;
    } catch (e) {
      // Not JSON? Use the raw value.
      return item.value;
    }
  };

  const mapped = {
    card1: normalizeValue(getValue("Card1") ?? getValue("card1") ?? getValue("CARD1")),
    card2: normalizeValue(getValue("Card2") ?? getValue("card2") ?? getValue("CARD2")),
    card3: normalizeValue(getValue("Card3") ?? getValue("card3") ?? getValue("CARD3")),
    c1Charge: normalizeValue(getValue("C1Charge") ?? getValue("c1Charge") ?? getValue("C1CHARGE")),
    c2Charge: normalizeValue(getValue("C2Charge") ?? getValue("c2Charge") ?? getValue("C2CHARGE")),
    c3Charge: normalizeValue(getValue("C3Charge") ?? getValue("c3Charge") ?? getValue("C3CHARGE")),
    updatedAt: new Date().toLocaleString(),
  };

  console.log("mapDeviceProperties: mapped result:", mapped);

  return mapped;
}

function normalizeValue(value) {
  if (value === null || value === undefined) {
    return "--";
  }
  return value;
}

/**
 * Fetch the latest device properties via GET
 * https://iot-api.heclouds.com/thingmodel/query-device-property?product_id=xxx&device_name=xxx
 *
 * @returns {Promise<{ card1: string, card2: string, card3: string, c1Charge: string, c2Charge: string, c3Charge: string, updatedAt: string }>}
 */
async function fetchDeviceProperties() {
  // Build the URL with query parameters
  const url = new URL(ONE_NET_CONFIG.getInfoUrl);
  url.searchParams.set("product_id", ONE_NET_CONFIG.productId);
  url.searchParams.set("device_name", ONE_NET_CONFIG.deviceName);

  const response = await fetch(url.toString(), {
    method: "GET",
    headers: {
      authorization: ONE_NET_CONFIG.authorization,
    },
  });

  if (!response.ok) {
    throw new Error(`Query failed: HTTP ${response.status}`);
  }

  const data = await response.json();
  
  console.log("OneNET raw response:", JSON.stringify(data, null, 2));

  if (typeof data.code !== "number") {
    throw new Error("Query failed: unexpected response format");
  }
  if (data.code !== 0) {
    throw new Error(`Query failed: ${data.msg || `code ${data.code}`}`);
  }

  if (data.data && Array.isArray(data.data)) {
    console.log("Returned data length:", data.data.length);
    console.log("Returned data array:", JSON.stringify(data.data, null, 2));
  }

  return mapDeviceProperties(data);
}

/**
 * Persist updated values back to OneNET.
 *
 * @param {{c1Charge?: number, c2Charge?: number, c3Charge?: number}} payload
 *   - fields are optional; undefined properties will be ignored
 */
async function updateDeviceProperties(payload) {
  // Build the params object accepted by the API
  const params = {};
  if (payload.c1Charge !== undefined) params.C1Charge = Number(payload.c1Charge);
  if (payload.c2Charge !== undefined) params.C2Charge = Number(payload.c2Charge);
  if (payload.c3Charge !== undefined) params.C3Charge = Number(payload.c3Charge);

  const response = await fetch(ONE_NET_CONFIG.setInfoUrl, {
    method: "POST",
    headers: {
      "content-type": "application/json",
      authorization: ONE_NET_CONFIG.authorization,
    },
    body: JSON.stringify({
      product_id: ONE_NET_CONFIG.productId,
      device_name: ONE_NET_CONFIG.deviceName,
      params: params, // payload-derived params
    }),
  });
  const data = await response.json();
  if (typeof data.code !== "number") {
    throw new Error("Update failed: unexpected response format");
  }

  if (data.code !== 0) {
    throw new Error(`Update failed: ${data.msg || `code ${data.code}`}`);
  }

  if (data.data && data.data.code !== undefined && data.data.code !== 200) {
    throw new Error(
      `Device did not acknowledge update: ${data.data.msg || `code ${data.data.code}`}`
    );
  }

  return data;
}

/**
 * Expose a slim API for app.js. In a bundler setup these would be exports.
 */
window.OneNetApi = {
  config: ONE_NET_CONFIG,
  fetchDeviceProperties,
  updateDeviceProperties,
};

