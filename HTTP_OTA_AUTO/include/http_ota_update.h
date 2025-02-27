#include <Update.h>
#include <HTTPClient.h>

void http_ota_update(String server)
{
    HTTPClient http;
    Serial.println("Checking for updates");
    http.begin(server);
    if (http.GET() == 200)
    {
        Serial.printf("Response OK, payload size: %d\n", http.getSize());

        WiFiClient *client = http.getStreamPtr();

        if (Update.begin(http.getSize()))
        {
            Serial.println("Update begin");
            if (Update.writeStream(*client) != http.getSize())
            {
                Serial.printf("Update failed, error code %d\n", Update.getError());
                Update.printError(Serial);
            }
            if (Update.isFinished())
            {
                Serial.println("Update finished");
                if (Update.end())
                {
                    Serial.println("Update successful");

                    ESP.restart();
                }
                else
                {
                    Serial.printf("Update failed, error code %d\n", Update.getError());
                    Update.printError(Serial);
                }
            }
        }
        else
            Serial.println("Update failed");
    }
    else
        Serial.printf("Connection unsuccessful: %d\n", http.GET());
}