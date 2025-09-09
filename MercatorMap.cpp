#include "framework.h"

const char* vertSource = R"(
    #version 330
    layout(location = 0) in vec2 vertexXY;
    layout(location = 1) in vec2 vertexUV;
    
    out vec2 texCoord;
    
    uniform mat4 MVP;
    
    void main() {
        texCoord = vertexUV;
        gl_Position = MVP * vec4(vertexXY, 0, 1);
    }
)";

const char* fragSource = R"(
    #version 330
    precision highp float;

    uniform bool useTexture;
    uniform sampler2D textureUnit; 
    uniform vec3 color;
    uniform vec3 sunDirection;
    uniform bool isMap; 
    in vec2 texCoord;
    out vec4 outColor;

  

    void main() {
        vec4 baseColor;


        if (useTexture && isMap) {
            baseColor = texture(textureUnit, texCoord);

            float longitude = texCoord.x * 2.0 * 3.14159265359 - 3.14159265359; 
            float latitude = 1.57079632679 - texCoord.y * 3.14159265359;       
            vec3 surfaceNormal = normalize(vec3(
                cos(latitude) * cos(longitude),
                cos(latitude) * sin(longitude),
                sin(latitude)
            ));
            bool isDaytime = dot(surfaceNormal, sunDirection) < 0.0;
            vec3 adjustedColor = isDaytime ? baseColor.rgb : baseColor.rgb * vec3(0.5);
            outColor = vec4(adjustedColor, baseColor.a);
        } else {
            // Use flat color for points/lines
            outColor = vec4(color, 1.0);
    }
}
)";




const int winWidth = 600,
winHeight = 600;
GPUProgram* gpuProgram;

float CalculateDistance(vec2 lonLat1, vec2 lonLat2, float earthRadius = 6371.0f) {
    float dLat = lonLat2.y - lonLat1.y;
    float dLon = lonLat2.x - lonLat1.x;
    float a = sin(dLat / 2.0f) * sin(dLat / 2.0f) +
        cos(lonLat1.y) * cos(lonLat2.y) * sin(dLon / 2.0f) * sin(dLon / 2.0f);
    float c = 2.0f * atan2(sqrt(a), sqrt(1.0f - a));
    return earthRadius * c;
}



class Map {
    unsigned int textureID;
    unsigned int vao, vbo;
public:
    Map(const unsigned char* compressedData, int dataSize, int width, int height) {
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        std::vector<unsigned char> decodedData;
        decodedData.reserve(width * height * 4);
        DecodeRLE(compressedData, dataSize, decodedData);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, &decodedData[0]);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        float vertices[] = {
            -1, -1, 0, 0,  
             1, -1, 1, 0,  
             1,  1, 1, 1,  
            -1,  1, 0, 1   
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
    }
    void DecodeRLE(const unsigned char* compressedData, int dataSize, std::vector<unsigned char>& decodedData) {
        for (int i = 0; i < dataSize; i++) {
            unsigned char byte = compressedData[i];
            unsigned char H = byte >> 2;
            unsigned char I = byte & 0x03;
            vec4 color;
            switch (I) {
            case 0: color = vec4(1, 1, 1, 1); break;
            case 1: color = vec4(0, 0, 1, 1); break;
            case 2: color = vec4(0, 1, 0, 1); break;
            case 3: color = vec4(0, 0, 0, 1); break;
            }
            for (int j = 0; j < int(H) + 1; j++) {
                decodedData.push_back((unsigned char)(color.x * 255));
                decodedData.push_back((unsigned char)(color.y * 255));
                decodedData.push_back((unsigned char)(color.z * 255));
                decodedData.push_back((unsigned char)(color.w * 255));
            }
        }
    }

    void Draw(vec3 sunDirection) {
        gpuProgram->Use();
        gpuProgram->setUniform(sunDirection, "sunDirection");
        gpuProgram->setUniform(true, "useTexture");
		gpuProgram->setUniform(true, "isMap");
        gpuProgram->setUniform(0, "textureUnit");

        mat4 MVP = mat4(1.0f);
        gpuProgram->setUniform(MVP, "MVP");

        glBindTexture(GL_TEXTURE_2D, textureID);
        glBindVertexArray(vao);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

};
float RadFromDeg(float degrees) {
    return degrees * (M_PI / 180.0f);
}

float MercatorY(float latitude) {
    return log(tan(M_PI / 4.0f + latitude / 2.0f));
}

vec2 ScreenToMercator(vec2 screenCoords) {
    float mercatorX = screenCoords.x * M_PI;
    float mercatorY = screenCoords.y * MercatorY(RadFromDeg(85));
    return vec2(mercatorX, mercatorY);
}

vec2 MercatorToClip(vec2 mercatorCoords) {
    return vec2(mercatorCoords.x / M_PI,
        mercatorCoords.y / MercatorY(RadFromDeg(85)));
}

vec2 MercatorToLonLat(vec2 mercatorCoords) {
    float longitude = mercatorCoords.x;
    float latitude = 2.0f * atan(exp(mercatorCoords.y)) - M_PI / 2.0f;
    return vec2(longitude, latitude);
}

vec2 LonLatToMercator(vec2 lonLat) {
    float mercatorX = lonLat.x;
    float mercatorY = log(tan(M_PI / 4.0f + lonLat.y / 2.0f));
    return vec2(mercatorX, mercatorY);
}

vec3 LonLatToCartesian(vec2 lonLat, float radius) {
    float lon = lonLat.x;
    float lat = lonLat.y;
    float x = radius * cos(lat) * cos(lon);
    float y = radius * cos(lat) * sin(lon);
    float z = radius * sin(lat);
    return vec3(x, y, z);
}

vec2 CartesianToLonLat(vec3 cart) {
    float r = length(cart);
    float lat = asin(cart.z / r);
    float lon = atan2(cart.y, cart.x);
    return vec2(lon, lat);
}

class Path {
    std::vector<std::vector<vec2>> pathSegments;
    unsigned int vao, vbo;
public:
    Path() {
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2), (void*)0);
        glEnableVertexAttribArray(0);
    }
    void SetPath(vec2 startMercator, vec2 endMercator, int numPoints = 100) {
        pathSegments.clear();

        vec2 startLonLat = vec2(startMercator.x, 2.0f * atan(exp(startMercator.y)) - M_PI / 2.0f);
        vec2 endLonLat = vec2(endMercator.x, 2.0f * atan(exp(endMercator.y)) - M_PI / 2.0f);

        float delta = endLonLat.x - startLonLat.x;
        if (delta > M_PI)      endLonLat.x -= 2.0f * M_PI;
        else if (delta < -M_PI) endLonLat.x += 2.0f * M_PI;

        vec3 startCartesian = LonLatToCartesian(startLonLat, 1.0f);
        vec3 endCartesian = LonLatToCartesian(endLonLat, 1.0f);

        float cosAngle = dot(normalize(startCartesian), normalize(endCartesian));
        cosAngle = fmin(fmax(cosAngle, -1.0f), 1.0f);
        float angle = acos(cosAngle);

        std::vector<vec2> currentSegment;
        vec2 prevClipCoord;
        bool hasPrev = false;
        const float threshold = 0.5f;

        for (int i = 0; i <= numPoints; i++) {
            float t = float(i) / numPoints;
            float A = sin((1 - t) * angle) / sin(angle);
            float B = sin(t * angle) / sin(angle);
            vec3 interpCartesian = normalize(A * startCartesian + B * endCartesian);
            vec2 interpLonLat = CartesianToLonLat(interpCartesian);

            if (interpLonLat.x > M_PI)      interpLonLat.x -= 2.0f * M_PI;
            else if (interpLonLat.x < -M_PI) interpLonLat.x += 2.0f * M_PI;

            vec2 interpMercator = LonLatToMercator(interpLonLat);
            vec2 clipCoord = MercatorToClip(interpMercator);

            if (hasPrev && fabs(clipCoord.x - prevClipCoord.x) > threshold) {
                pathSegments.push_back(currentSegment);
                currentSegment.clear();
            }

            currentSegment.push_back(clipCoord);
            prevClipCoord = clipCoord;
            hasPrev = true;
        }

        if (!currentSegment.empty())
            pathSegments.push_back(currentSegment);
    }


    void Draw() {
        if (pathSegments.empty())
            return;
        gpuProgram->Use();
        gpuProgram->setUniform(false, "useTexture");
		gpuProgram->setUniform(false, "isMap");
        gpuProgram->setUniform(vec3(1.0f, 1.0f, 0.0f), "color");

        glBindVertexArray(vao);
        glLineWidth(3.0f);

        for (const auto& segment : pathSegments) {
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, segment.size() * sizeof(vec2), segment.data(), GL_STATIC_DRAW);
            glDrawArrays(GL_LINE_STRIP, 0, segment.size());
        }
    }

};

class Station {
    vec2 mercatorCoords;
    unsigned int vao, vbo;
    bool initialized;
public:
    Station(vec2 mercatorCoords) : mercatorCoords(mercatorCoords), initialized(false) {}
    vec2 GetMercatorCoords() const { return mercatorCoords; }

    void Init() {
        if (initialized) return;

        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        vec2 clipCoords = MercatorToClip(mercatorCoords);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vec2), &clipCoords, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2), (void*)0);
        glEnableVertexAttribArray(0);

        initialized = true;
    }

    void Draw() {
        if (!initialized) Init();
        gpuProgram->Use();
        gpuProgram->setUniform(false, "useTexture");
		gpuProgram->setUniform(false, "isMap");
        gpuProgram->setUniform(vec3(1.0f, 0.0f, 0.0f), "color");
        glPointSize(10.0f);

        glBindVertexArray(vao);
        glDrawArrays(GL_POINTS, 0, 1);
    }
};

vec3 CalculateSunDirection(int hour) {
    float longitudeAngle = hour * (360.0f / 24.0f); 
    float latitudeAngle = RadFromDeg(23.0f); 
    float x = cos(latitudeAngle) * cos(RadFromDeg(longitudeAngle));
    float y = cos(latitudeAngle) * sin(RadFromDeg(longitudeAngle));
    float z = sin(latitudeAngle);
    return normalize(vec3(x, y, z)); 
}
vec3 SurfaceNormal(float latitude, float longitude) {
    float x = cos(latitude) * cos(longitude);
    float y = cos(latitude) * sin(longitude);
    float z = sin(latitude);
    return normalize(vec3(x, y, z)); 
}
bool IsDaytime(vec3 surfaceNormal, vec3 sunDirection) {
    return dot(surfaceNormal, sunDirection) > 0.0f; 
}
vec3 CalculateLightColor(vec3 surfaceNormal, vec3 sunDirection) {
	if (IsDaytime(surfaceNormal, sunDirection)) {
		return vec3(1.0f, 1.0f, 0.8f); 
	}
	else {
		return vec3(0.2f, 0.2f, 0.5f); 
	}
}

class MyWindow : public glApp {
    std::vector<Station> stations;
    std::vector<Path> paths;
    Map* map;
    int currentHour; 
    vec3 sunDirection; 
public:
    MyWindow() : glApp("Mercator Map"), currentHour(0), map(nullptr) {
        sunDirection = CalculateSunDirection(currentHour);
    }
    void onInitialization() {
        glViewport(0, 0, winWidth, winHeight);
        gpuProgram = new GPUProgram(vertSource, fragSource);
       
        const unsigned char compressedData[] = {
          252, 252, 252, 252, 252, 252, 252, 252, 252, 0, 9, 80, 1, 148, 13, 72, 13, 140, 25, 60, 21, 132, 41, 12, 1, 28,
25, 128, 61, 0, 17, 4, 29, 124, 81, 8, 37, 116, 89, 0, 69, 16, 5, 48, 97, 0, 77, 0, 25, 8, 1, 8, 253, 253, 253, 253,
101, 10, 237, 14, 237, 14, 241, 10, 141, 2, 93, 14, 121, 2, 5, 6, 93, 14, 49, 6, 57, 26, 89, 18, 41, 10, 57, 26,
89, 18, 41, 14, 1, 2, 45, 26, 89, 26, 33, 18, 57, 14, 93, 26, 33, 18, 57, 10, 93, 18, 5, 2, 33, 18, 41, 2, 5, 2, 5, 6,
89, 22, 29, 2, 1, 22, 37, 2, 1, 6, 1, 2, 97, 22, 29, 38, 45, 2, 97, 10, 1, 2, 37, 42, 17, 2, 13, 2, 5, 2, 89, 10, 49,
46, 25, 10, 101, 2, 5, 6, 37, 50, 9, 30, 89, 10, 9, 2, 37, 50, 5, 38, 81, 26, 45, 22, 17, 54, 77, 30, 41, 22, 17, 58,
1, 2, 61, 38, 65, 2, 9, 58, 69, 46, 37, 6, 1, 10, 9, 62, 65, 38, 5, 2, 33, 102, 57, 54, 33, 102, 57, 30, 1, 14, 33, 2,
9, 86, 9, 2, 21, 6, 13, 26, 5, 6, 53, 94, 29, 26, 1, 22, 29, 0, 29, 98, 5, 14, 9, 46, 1, 2, 5, 6, 5, 2, 0, 13, 0, 13,
118, 1, 2, 1, 42, 1, 4, 5, 6, 5, 2, 4, 33, 78, 1, 6, 1, 6, 1, 10, 5, 34, 1, 20, 2, 9, 2, 12, 25, 14, 5, 30, 1, 54, 13, 6,
9, 2, 1, 32, 13, 8, 37, 2, 13, 2, 1, 70, 49, 28, 13, 16, 53, 2, 1, 46, 1, 2, 1, 2, 53, 28, 17, 16, 57, 14, 1, 18, 1, 14,
1, 2, 57, 24, 13, 20, 57, 0, 2, 1, 2, 17, 0, 17, 2, 61, 0, 5, 16, 1, 28, 25, 0, 41, 2, 117, 56, 25, 0, 33, 2, 1, 2, 117,
52, 201, 48, 77, 0, 121, 40, 1, 0, 205, 8, 1, 0, 1, 12, 213, 4, 13, 12, 253, 253, 253, 141
        };
        map = new Map(compressedData, sizeof(compressedData), 64, 64);
    }
    void onDisplay() {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        map->Draw(sunDirection);
        for (Path& path : paths)
            path.Draw();

        for (Station& station : stations)
            station.Draw();
    }
    void onMousePressed(MouseButton button, int pX, int pY) {
        float cX = 2.0f * pX / winWidth - 1.0f;
        float cY = 1.0f - 2.0f * pY / winHeight;
        vec2 screenCoords = vec2(cX, cY);

       
        vec2 mercatorCoords = ScreenToMercator(screenCoords);
        stations.push_back(Station(mercatorCoords));

        
        if (stations.size() > 1) {
            vec2 startMercator = stations[stations.size() - 2].GetMercatorCoords();
            vec2 endMercator = stations[stations.size() - 1].GetMercatorCoords();

            Path newPath;
            newPath.SetPath(startMercator, endMercator);
            paths.push_back(newPath);

            float distance = CalculateDistance(
                MercatorToLonLat(startMercator),
                MercatorToLonLat(endMercator)
            );
            printf("Distance between stations: %.2f km\n", distance);
        }

        refreshScreen();
    }

    void onKeyboard(int key) {
        if (key == 'n') {
            currentHour = (currentHour + 1) % 24; 
            sunDirection = CalculateSunDirection(currentHour);
            refreshScreen(); 
        }
    }

};

MyWindow app;