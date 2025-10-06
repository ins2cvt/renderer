struct UniformBuffer
{
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;

[vk::push_constant]
ConstantBuffer<float3> cameraAngle;

struct VertexInput
{
    float3 pos;
};

struct VertexOutput {
    float4 pos : SV_Position;
    float3 color;
};

[shader("vertex")]
VertexOutput vertexShader(VertexInput input) {
    VertexOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.pos, 1.0f))));
    // TODO: Make this use the camera angle push constant.
    output.color = float3(1.0f, 1.0f, input.pos.z * 10.0f);
    return output;
};

[shader("fragment")]
float4 fragmentShader(VertexOutput vertexOutput) : SV_Target
{
    float4 color = float4(vertexOutput.color, 1.0f);
    return color;
}
