using System;
using System.IO;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace send_picture_to_cloud_iot
{
    public partial class form_backend : Form
    {
        public form_backend()
        {
            InitializeComponent();
            CheckObstacleSignal();
        }

        string firebase_rtdbUrl = "https://phan-loai-rac-default-rtdb.asia-southeast1.firebasedatabase.app";

        string supabaseUrl = "https://prvpkyyggxodihenerie.supabase.co";
        string supabaseKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InBydnBreXlnZ3hvZGloZW5lcmllIiwicm9sZSI6ImFub24iLCJpYXQiOjE3Nzk0MDY1MjAsImV4cCI6MjA5NDk4MjUyMH0.REcgjw9KD7yJqMCHr3XUNmkzzpTAA7pSxXUbLqU2dy4";
        string bucketName = "trash-images";

        string geminiApiKey = "Your-api-key-here";

        string selectedImagePath = "";

        // Hàm liên tục kiểm tra tín hiệu từ ESP32
        private async Task CheckObstacleSignal()
        {
            try
            {
                using (HttpClient client = new HttpClient())
                {
                    string res = await client.GetStringAsync($"{firebase_rtdbUrl}/sensors/pir/obstacle.json");
                    if (res != null && res.Trim() == "1")
                    {

                        MessageBox.Show("Phát hiện vật cản từ thùng rác! Hãy chọn ảnh rác để phân loại.");

                        // Kích hoạt luồng chọn ảnh
                        await TriggerSelectionAndClassification();
                    }
                    else
                    {
                        MessageBox.Show("Không có rác để phân loại");
                            btnUpload.Enabled = false; // Vô hiệu hóa nút upload nếu không có rác
                    }
                }
            }
            catch
            { // Bỏ qua lỗi mạng nhất thời
            }

        }

        private async Task TriggerSelectionAndClassification()
        {
            OpenFileDialog dialog = new OpenFileDialog();
            dialog.Filter = "Image Files|*.jpg;*.jpeg;*.png";

            if (dialog.ShowDialog() == DialogResult.OK)
            {
                string selectedImagePath = dialog.FileName;
                pictureBox1.ImageLocation = selectedImagePath;

                // 1. Đẩy lên Supabase làm lịch sử lưu trữ
                await UploadImageToSupabase(selectedImagePath);

                // 2. Gọi AI Gemini lấy chữ H, N, G
                string aiResult = await ClassifyTrashWithGemini(selectedImagePath);

                // 3. Gửi kết quả ngược lại Firebase cho ESP32 nhận
                if (aiResult == "H" || aiResult == "N" || aiResult == "G")
                {
                    await SendResultToFirebase(aiResult);
                }
            }
        }

        // Hàm gửi chữ H, N, G lên Firebase
        private async Task SendResultToFirebase(string result)
        {
            try
            {
                using (HttpClient client = new HttpClient())
                {
                    // Đẩy kết quả lên nhánh category dưới dạng chuỗi string mã hóa JSON
                    var content = new StringContent($"\"{result}\"", Encoding.UTF8, "application/json");
                    await client.PutAsync($"{firebase_rtdbUrl}/trash_type/category.json", content);
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show("Lỗi gửi tín hiệu điều khiển: " + ex.Message);
            }
        }

        private async void btnUpload_Click(object sender, EventArgs e)
        {
            OpenFileDialog dialog = new OpenFileDialog();
            dialog.Filter = "Image Files|*.jpg;*.jpeg;*.png";

            if (dialog.ShowDialog() == DialogResult.OK)
            {
                selectedImagePath = dialog.FileName;
                pictureBox1.ImageLocation = selectedImagePath;

                MessageBox.Show("Đang đẩy ảnh lên Supabase... Vui lòng đợi!");
                await UploadImageToSupabase(selectedImagePath);
                MessageBox.Show("Đang phân loại rác bằng AI... Vui lòng đợi!");
                await ClassifyTrashWithGemini(selectedImagePath);
            }
        }

        private async Task UploadImageToSupabase(string filePath)
        {
            try
            {
                // Đọc file ảnh dưới dạng byte array
                byte[] fileBytes = File.ReadAllBytes(filePath);

                // Tên file trên Supabase (Ghi đè liên tục để không tốn dung lượng)
                string fileName = "current_trash.jpg";

                // Xây dựng đường dẫn API chuẩn của Supabase Storage
                string requestUrl = $"{supabaseUrl}/storage/v1/object/{bucketName}/{fileName}";

                using (HttpClient client = new HttpClient())
                {
                    // Gắn khóa xác thực
                    client.DefaultRequestHeaders.Add("apikey", supabaseKey);
                    client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", supabaseKey);

                    // Đóng gói nội dung ảnh
                    ByteArrayContent content = new ByteArrayContent(fileBytes);
                    content.Headers.ContentType = new MediaTypeHeaderValue("image/jpeg");

                    // Gửi request upload file (Dùng POST hoặc PUT)
                    HttpResponseMessage response = await client.PutAsync(requestUrl, content);

                    if (response.IsSuccessStatusCode)
                    {
                        // Tạo đường link Public để AI hoặc ứng dụng khác truy cập
                        string publicImageUrl = $"{supabaseUrl}/storage/v1/object/public/{bucketName}/{fileName}";

                        MessageBox.Show("Upload Supabase thành công!\nLink ảnh: " + publicImageUrl);

                        // Ở bước tiếp theo, bạn chỉ cần ném biến 'publicImageUrl' này vào API của Gemini là xong!
                    }
                    else
                    {
                        string error = await response.Content.ReadAsStringAsync();
                        MessageBox.Show("Lỗi từ Supabase: " + error);
                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show("Lỗi hệ thống: " + ex.Message);
            }
        }
        private async Task<string> ClassifyTrashWithGemini(string filePath)
        {
            try
            {
                // 1. Chuyển ảnh thành chuỗi Base64
                byte[] imageBytes = File.ReadAllBytes(filePath);
                string base64Image = Convert.ToBase64String(imageBytes);

                // 2. Thiết lập đường dẫn API của Gemini 1.5 Flash
                string apiUrl = $"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key={geminiApiKey}";

                // 3. Kỹ thuật Prompt Engineering: Ép AI chỉ trả về 1 ký tự
                string prompt = "Bạn là hệ thống AI phân loại rác. Dựa vào ảnh, hãy trả về đúng 1 ký tự duy nhất: 'H' nếu là rác hữu cơ (thức ăn, rau củ...), 'N' nếu là rác vô cơ/nhựa/kim loại, 'G' nếu là giấy/carton. TUYỆT ĐỐI KHÔNG giải thích gì thêm.";

                // 4. Xây dựng cấu trúc dữ liệu JSON để gửi đi
                var requestBody = new
                {
                    contents = new[]
                    {
                        new
                        {
                            parts = new object[]
                            {
                                new { text = prompt },
                                new { inline_data = new { mime_type = "image/jpeg", data = base64Image } }
                            }
                        }
                    }
                };

                string jsonContent = JsonConvert.SerializeObject(requestBody);
                var httpContent = new StringContent(jsonContent, Encoding.UTF8, "application/json");

                using (HttpClient client = new HttpClient())
                {
                    // 5. Gửi request đến Google Server
                    HttpResponseMessage response = await client.PostAsync(apiUrl, httpContent);
                    string responseString = await response.Content.ReadAsStringAsync();

                    if (response.IsSuccessStatusCode)
                    {
                        // 6. Bóc tách cục JSON để lấy kết quả
                        JObject jsonResponse = JObject.Parse(responseString);

                        // Truy cập vào: candidates[0] -> content -> parts[0] -> text
                        string aiResult = jsonResponse["candidates"][0]["content"]["parts"][0]["text"].ToString().Trim();

                        MessageBox.Show($"AI Phân Loại Trả Về: [ {aiResult} ]");

                        // TẠI ĐÂY: Nếu aiResult == "H", bạn sẽ gọi lệnh gửi chữ "H" lên Firebase Realtime Database
                        // để ESP32 trên Wokwi đọc được và mở nắp thùng.
                        return aiResult;
                    }
                    else
                    {
                        MessageBox.Show("Lỗi từ Gemini: " + responseString);
                        return "";
                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show("Lỗi kết nối AI: " + ex.Message);
                return "";
            }
        }
    }
}
