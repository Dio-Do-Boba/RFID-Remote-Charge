/**
 * vehicleApi.js
 *
 * Responsibilities:
 *   - handle interactions with the vehicle IoT system API
 *   - provide functions to initialize and call vehicles
 */

/**
 * Vehicle IoT system configuration
 */
const VEHICLE_API_CONFIG = {
  baseUrl: "https://mono.kiyo.dev",
  /**
   * Mapping between cards and vehicles/stations
   * Card1 -> vehicle "a" -> station 1
   * Card2 -> vehicle "b" -> station 2
   * Card3 -> vehicle "c" -> station 3
   */
  cardMapping: {
    card1: { vehicle: "a", station: 1 },
    card2: { vehicle: "b", station: 2 },
    card3: { vehicle: "c", station: 3 },
  },
};

/**
 * Initialize vehicle positions
 * @param {{a: number, b: number, c: number}} positions - Initial positions for vehicles
 * @returns {Promise<any>}
 */
async function initializeVehicles(positions = { a: 1, b: 2, c: 3 }) {
  try {
    const response = await fetch(`${VEHICLE_API_CONFIG.baseUrl}/api/v1/initialize`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ positions }),
    });

    if (!response.ok) {
      throw new Error(`Initialize failed: HTTP ${response.status}`);
    }

    const data = await response.json();
    console.log("Vehicle initialization response:", data);
    return data;
  } catch (error) {
    console.error("Failed to initialize vehicles:", error);
    throw error;
  }
}

/**
 * Call a vehicle to a specific station
 * @param {string} vehicle - Vehicle ID ("a", "b", or "c")
 * @param {number} station - Station number (1, 2, 3, or 4)
 * @returns {Promise<any>}
 */
async function callVehicle(vehicle, station) {
  try {
    const response = await fetch(`${VEHICLE_API_CONFIG.baseUrl}/api/v1/call`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ vehicle, station }),
    });

    if (!response.ok) {
      throw new Error(`Call vehicle failed: HTTP ${response.status}`);
    }

    const data = await response.json();
    console.log(`Vehicle ${vehicle} called to station ${station}:`, data);
    return data;
  } catch (error) {
    console.error(`Failed to call vehicle ${vehicle} to station ${station}:`, error);
    throw error;
  }
}

/**
 * Call vehicle based on card number
 * @param {"card1"|"card2"|"card3"} cardKey - Card identifier
 * @returns {Promise<any>}
 */
async function callVehicleByCard(cardKey) {
  const mapping = VEHICLE_API_CONFIG.cardMapping[cardKey];
  if (!mapping) {
    throw new Error(`Unknown card key: ${cardKey}`);
  }

  return callVehicle(mapping.vehicle, mapping.station);
}

/**
 * Expose API for app.js
 */
window.VehicleApi = {
  config: VEHICLE_API_CONFIG,
  initializeVehicles,
  callVehicle,
  callVehicleByCard,
};

