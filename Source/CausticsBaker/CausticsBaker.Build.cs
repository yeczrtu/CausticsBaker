using System.IO;
using UnrealBuildTool;

public class CausticsBaker : ModuleRules
{
    public CausticsBaker(ReadOnlyTargetRules Target) : base(Target)
    {
        if (Target.Platform != UnrealTargetPlatform.Win64)
        {
            throw new BuildException("CausticsBaker supports Win64 only.");
        }

        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ApplicationCore",
            "AssetRegistry",
            "AssetTools",
            "ContentBrowser",
            "DeveloperSettings",
            "EditorSubsystem",
            "InputCore",
            "IntelOIDN",
            "LevelEditor",
            "Projects",
            "PropertyEditor",
            "RenderCore",
            "Renderer",
            "RHI",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "UnrealEd"
        });

        PrivateIncludePaths.AddRange(new[]
        {
            Path.Combine(GetModuleDirectory("Renderer"), "Internal"),
            Path.Combine(GetModuleDirectory("Renderer"), "Private"),
            Path.Combine(GetModuleDirectory("RenderCore"), "Public")
        });
    }
}
