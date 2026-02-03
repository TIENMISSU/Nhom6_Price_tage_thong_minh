const mqtt = require("mqtt");
const { google } = require("googleapis");

// === 1. CẤU HÌNH ===
const KEY_FILE = "pricetag-482903-a16154fd2ff3.json"; 
const SHEET_ID = "1z6xF-rMS03wI7x33aTFDGzNWdK3eyXafCtL8mhUas8";
const RANGE = "Sheet1!A2:E"; 

const MQTT_HOST = "mqtt://broker.hivemq.com:1883";
const MQTT_TOPIC = "price_tag/data"; 

let lastPayloadString = "";

// === 2. KHỞI TẠO ===
const auth = new google.auth.GoogleAuth({
  keyFile: KEY_FILE,
  scopes: ["https://www.googleapis.com/auth/spreadsheets.readonly"]
});

const sheets = google.sheets({ version: "v4", auth });
const client = mqtt.connect(MQTT_HOST);

client.on("connect", () => {
  console.log("✅ MQTT Connected!");
  run(); 
  setInterval(run, 5000); 
});

async function run() {
  try {
    const res = await sheets.spreadsheets.values.get({
      spreadsheetId: SHEET_ID,
      range: RANGE
    });

    const rows = res.data.values;
    if (!rows || rows.length === 0) return;

    // Lấy dòng cuối cùng
    const lastRow = rows[rows.length - 1];

    // --- TÍNH TOÁN GIÁ MỚI ---
    let rawPrice = lastRow[3] || "0";     // Ví dụ: "$1,200.00"
    let rawSale  = lastRow[4] || "0%";    // Ví dụ: "20%"

    // 1. Chuyển đổi giá tiền từ String sang Số
    // Xóa dấu $ và dấu , (comma) để tính toán
    let priceNumber = parseFloat(rawPrice.replace(/[$,]/g, "")); 
    
    // 2. Chuyển đổi % giảm giá
    let salePercent = parseFloat(rawSale.replace("%", ""));
    if (isNaN(salePercent)) salePercent = 0;

    // 3. Tính giá sau giảm (New Price)
    let newPriceNumber = priceNumber * (1 - salePercent / 100);

    // 4. Định dạng lại thành tiền tệ ($...)
    // Hàm này sẽ tự thêm dấu phẩy ngăn cách hàng nghìn
    let formatter = new Intl.NumberFormat('en-US', {
      style: 'currency',
      currency: 'USD',
    });

    let finalNewPrice = formatter.format(newPriceNumber); // "$960.00"
    let finalOldPrice = rawPrice; // Giữ nguyên giá gốc để hiển thị gạch ngang

    const payload = {
      code:      lastRow[1] || "000000",
      name:      lastRow[2] || "Unknown",
      old_price: finalOldPrice, // Giá gốc (để gạch chéo)
      new_price: finalNewPrice, // Giá mới (để in to)
      sale:      (salePercent > 0) ? rawSale : "" // Chỉ gửi % nếu > 0
    };

    const currentPayloadString = JSON.stringify(payload);

    if (currentPayloadString !== lastPayloadString) {
      console.log("------------------------------------------------");
      console.log("!!! PHÁT HIỆN DỮ LIỆU MỚI !!!");
      
      // Thêm dòng này để kiểm tra Tên và ID
      console.log(`📦 Tên SP: ${payload.name} | Mã: ${payload.code}`);
      console.log(`💲 Giá Gốc: ${payload.old_price} | Giảm: ${payload.sale} | => GIÁ MỚI: ${payload.new_price}`);
      
      client.publish(MQTT_TOPIC, currentPayloadString, { qos: 0, retain: true });
      lastPayloadString = currentPayloadString;
    } 
  } catch (error) {
    console.error("LOI:", error.message);
  }
}